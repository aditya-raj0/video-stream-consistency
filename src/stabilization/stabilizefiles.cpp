/*  File-based stabilization

    Copyright (C) 2023 Max Reimann (max.reimann@hpi.de)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/

#include "stabilizefiles.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <optional>

#include <cmath>
#include <QDir>
#include <QStringList>
#include <QImage>
#include <QDebug>   
#include <QSharedPointer>
#include <QList>
#include <QElapsedTimer>

#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>

#define WARMUP_FRAMES (k+5)

// FileStabilizer::FileStabilizer(QDir originalFrameDir, QDir processedFrameDir, QDir stabilizedFrameDir, std::optional<QDir> opticalFlowDir,  
//     std::optional<QString> modelType, int width, int height, int batchSize, bool computeOpticalFlow,
//     const QString &configFilePath):
FileStabilizer::FileStabilizer(std::string origPath, std::string procPath, std::string stabPath, 
    std::optional<QDir> opticalFlowDir, std::optional<QString> modelType, 
    int width, int height, int batchSize, int frameCount, bool computeOpticalFlow) :
    VideoStabilizer(width, height, batchSize, modelType, computeOpticalFlow),
    originalMemmapPath(origPath), 
    processedMemmapPath(procPath), 
    stabilizedMemmapPath(stabPath),
    opticalFlowDir(opticalFlowDir)
{
    frameSize = width * height * 3;
    // Calculate frameCount by dividing total file size by frame size
    // struct stat sb;
    // if (stat(originalMemmapPath.c_str(), &sb) == 0) {
    //     frameCount = sb.st_size / frameSize;
    //     qDebug() << "Calculated frameCount:" << frameCount;
    // } else {
    //     throw std::runtime_error("Failed to get file size for frameCount calculation.");
    // }
    

    int fd_orig = open(originalMemmapPath.c_str(), O_RDONLY);
    int fd_proc = open(processedMemmapPath.c_str(), O_RDONLY);
    int fd_stab = open(stabilizedMemmapPath.c_str(), O_RDWR);

    if (fd_orig == -1 || fd_proc == -1 || fd_stab == -1) {
        throw std::runtime_error("Failed to open one or more memmap files");
    }

    originalMemmap = static_cast<uint8_t*>(mmap(nullptr, frameSize * frameCount, PROT_READ, MAP_SHARED, fd_orig, 0));
    processedMemmap = static_cast<uint8_t*>(mmap(nullptr, frameSize * frameCount, PROT_READ, MAP_SHARED, fd_proc, 0));
    stabilizedMemmap = static_cast<uint8_t*>(mmap(nullptr, frameSize * frameCount, PROT_WRITE, MAP_SHARED, fd_stab, 0));


    if (originalMemmap == MAP_FAILED || processedMemmap == MAP_FAILED || stabilizedMemmap == MAP_FAILED) {
        throw std::runtime_error("Failed to mmap one or more files");
    }

    close(fd_orig);
    close(fd_proc);
    close(fd_stab);
} 

bool FileStabilizer::loadFrame(int i)
{
    if (i < 0 || i >= frameCount) return false;

    size_t orig_offset = i * frameSize;
    size_t proc_offset = i * frameSize;

    // Create QImages using the memory from the memmap (No deep copy)
    QImage originalImg((uchar*)&originalMemmap[orig_offset], width, height, QImage::Format_RGB888);
    QImage processedImg((uchar*)&processedMemmap[proc_offset], width, height, QImage::Format_RGB888);

    // Convert to RGBA8888 for internal processing
    QImage originalRGBA = originalImg.convertToFormat(QImage::Format_RGBA8888);
    QImage processedRGBA = processedImg.convertToFormat(QImage::Format_RGBA8888);

    // Store frames for processing
    originalFramesQt << QSharedPointer<QImage>(new QImage(originalRGBA));
    originalFrames << imageToGPU(originalRGBA);
    processedFrames << imageToGPU(processedRGBA);

    return true;
}


QString FileStabilizer::formatIndex(int index) {
    return QString("%1").arg(index, 6, 10, QChar('0'));
};


// void FileStabilizer::outputFrame(int i, QSharedPointer<QImage> q) {
//     q->save(stabilizedFrameDir.filePath(formatIndex(i) + ".jpg"));
// }

/*
    In order to make the functio Output frame produce png outputs you need to remove the A channel from the from RGBA that is why there is this extra step
    But the reason its commented here is that instead of convering it here you can produce the image without one in the first place to save comp. 
    check videostabilizer.cpp line no 293 for the source.
*/

void FileStabilizer::outputFrame(int i, QSharedPointer<QImage> q) {
    size_t offset = i * frameSize;

    // Convert the stabilized frame to RGB888 before writing
    QImage img = q->convertToFormat(QImage::Format_RGB888);
    memcpy(&stabilizedMemmap[offset], img.bits(), frameSize);
}


bool FileStabilizer::stabilizeAll() {
    if (originalMemmap && processedMemmap) {
        qDebug() << "Using memmap mode for stabilization";
        // Preload the first set of frames from the memmap files
        for (int j = 0; j < 2*k+batchSize; j++) {
            qDebug() << "Preloading frame" << j;
            bool success = loadFrame(j); 
            if (!success) {
                throw std::runtime_error("Failed to preload initial frames from memmap!");
            }
            
            // Write first k processed frames to output memmap
            if (j <= k) {
                auto output = QSharedPointer<QImage>(new QImage(gpuToImage(*processedFrames[j])));
                outputFrame(j, output);
            }
        }
        lastStabilizedFrame.copyFrom(*processedFrames.back());
    }
    else {
        // Fallback to original directory-based logic
        preloadProcessedFrames();
    }

    std::cout << "Starting stabilization..." << std::endl;
    for (int i = k;; i++) {
        timer.start();
        bool success = doOneStep(i); 
        if (!success) {
            break;
        }

        if (i == 100+WARMUP_FRAMES) {
            int count = i - WARMUP_FRAMES;
            qDebug() << "Per-frame time in ms averaged over 100 frames (without first 5 for warmup):" 
                << "load (wait+to_gpu): " << timeLoad / float(count) 
                << "optflow: " << timeOptFlow  / float(count)
                << "save: " << timeSave / float(count) 
                << "stabilize: " << timeStabilized / float(count) 
                << "overall: " << (timeLoad + timeOptFlow + timeStabilized + timeSave) / float(count);
        }
    }
    return true;
}


void FileStabilizer::retrieveOpticalFlow(int currentFrame)
{
    if (opticalFlowDir) {
        std::vector<float> flowTmp;
        int flowWidth, flowHeight;
        // flow file i descibes optical flow i-1 -> i     (last -> current)
        // bwd flow file describes optical flow i -> i-1  (current -> last)
        ReadFlowFile(flowTmp, flowWidth, flowHeight, opticalFlowDir->filePath("frame_" + formatIndex(currentFrame + 1) + ".flo").toStdString().c_str());
        initializeFlowImage(flowTmp, flowFwd, flowWidth, flowHeight, width, height);

        ReadFlowFile(flowTmp, flowWidth, flowHeight, opticalFlowDir->filePath("frame_" + formatIndex(currentFrame) + "_bwd.flo").toStdString().c_str()); 
        initializeFlowImage(flowTmp, flowBwd, flowWidth, flowHeight, width, height);
    } else {
        VideoStabilizer::retrieveOpticalFlow(currentFrame);
    }
}