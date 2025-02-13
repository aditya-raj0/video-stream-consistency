import numpy as np
import cv2
import os
from glob import glob

class memmap_tc_pipeline():

    def __init__(self):
        pass

    def frames_to_dat(self, orignal_frame_dir:str, processed_frame_dir:str, orignal_dat_file_path:str, processed_dat_file_path:str):
        orignal_files = sorted(
            glob(os.path.join(orignal_frame_dir, '*.jpg')) +
            glob(os.path.join(orignal_frame_dir, '*.png'))
        )
        processed_files = sorted(
            glob(os.path.join(processed_frame_dir, '*.jpg')) +
            glob(os.path.join(processed_frame_dir, '*.png'))
        )

        assert len(orignal_files) == len(processed_files), "The number of frames in orignal and prodessed directories are different"

        self.num_frames = len(orignal_files)
        self.height, self.width = cv2.imread(orignal_files[0]).shape[:2]
        self.channels = 3 # THIS IS ASSUMING THAT THE FRAMES IN THE DIRS ARE 3 CHANNEL AND NOT 4

        orignal_memmap = np.memmap(
            filename=orignal_dat_file_path,
            dtype=np.uint8,
            mode='w+',
            shape=(self.num_frames, self.height, self.width, self.channels)
        )
        processed_memmap = np.memmap(
            filename=processed_dat_file_path,
            dtype=np.uint8,
            mode='w+',
            shape=(self.num_frames, self.height, self.width, self.channels)
        )

        for i, (orignal_path, processed_path) in enumerate(zip(orignal_files, processed_files)):
            orignal_frame = cv2.imread(orignal_path, cv2.IMREAD_COLOR)
            processed_frame = cv2.imread(processed_path, cv2.IMREAD_COLOR)

            orignal_memmap[i]=orignal_frame
            processed_memmap[i]=processed_frame
        
        orignal_memmap.flush()
        processed_memmap.flush()

        print('DONE')
    

    def dat_to_frames(self, dat_file_path:str, output_dir:str):
        output_memmap = np.memmap(
            filename=dat_file_path,
            dtype=np.uint8,
            mode = 'r',
            shape=(self.num_frames, self.height, self.width, self.channels)
        )

        os.makedirs(output_dir, exist_ok=True)

        for i in range(self.num_frames):
            frame = output_memmap[i]
            frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGBA2BGR)
            cv2.imwrite(os.path.join(output_dir, f"{i:06d}.png"), frame_bgr)
        
        print('DONE')
    

    


if __name__ == "__main__":
    
    x = memmap_tc_pipeline()
    x.frames_to_dat(
        orignal_frame_dir = '',
        processed_frame_dir = '',
        orignal_dat_file_path = '',
        processed_dat_file_path = '',

    )