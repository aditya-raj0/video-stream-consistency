
#include "videostabilizer.h"

#include <QDir>
#include <optional>
#include <QStringList>
#include <QDebug>


struct pathInputs
{
    QStringList originalFramePaths;
    QStringList processedFramePaths;
    int frameCount;
};


class FileStabilizer : protected VideoStabilizer
{
    private:
        QDir originalFrameDir;
        QDir processedFrameDir;
        QDir stabilizedFrameDir;
        std::optional<QDir> opticalFlowDir;
        std::optional<pathInputs> inputPaths;
        int frameCount;
        std::string originalMemmapPath;
        std::string processedMemmapPath;
        std::string stabilizedMemmapPath;
        int frameSize;
        uint8_t* originalMemmap;
        uint8_t* processedMemmap;
        uint8_t* stabilizedMemmap;
    protected:
        QString formatIndex(int index);
        bool loadFrame(int i);
        void outputFrame(int i, QSharedPointer<QImage> q);
        void retrieveOpticalFlow(int currentFrame);
    public:
        // FileStabilizer(QDir originalFrameDir, QDir processedFrameDir, QDir stabilizedFrameDir, std::optional<QDir> opticalFlowDir, 
        //                 std::optional<QString> modelType, int width, int height, int batchSize, bool computeOpticalFlow,
        //                 const QString &configFilePath);
        // FileStabilizer(QDir originalFrameDir, QDir processedFrameDir, QDir stabilizedFrameDir, std::optional<QDir> opticalFlowDir, 
        //                 std::optional<QString> modelType, int width, int height, int batchSize, bool computeOpticalFlow);
        FileStabilizer(std::string originalMemmapPath, std::string processedMemmapPath, std::string stabilizedMemmapPath,
                        std::optional<QDir> opticalFlowDir, std::optional<QString> modelType,
                        int width, int height, int batchSize, int frameCount, bool computeOpticalFlow);

        bool stabilizeAll();
};
