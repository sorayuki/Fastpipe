# Abstract
Fastpipe is a tool that provides pipes with large buffers for inter-process communication. It offers better performance than simple ```|``` operator in the Windows command prompt.

# Usage example
    fp ffmpeg -i "input.mp4" -f yuv4mpegpipe - "|" ffmpeg -f yuv4mpegpipe -i - -c libx264 output.mp4

use ```"|"``` instead of ```|```. You can set environment variable "FP_BUFFERSIZE" specifies the buffer size in bytes.

    D:\tools\ffmpeg\ffmpeg-n9.0\bin>ffmpeg -i "C:\Users\SoraYuki\Videos\2025-09-24 00-10-28.mp4" -t 600 -f rawvideo -y - | ffmpeg -f rawvideo -pix_fmt yuv420p10le -s 1920x1200 -i - -f rawvideo -y nul
    (...)
    frame=18000 fps=284 q=-0.0 Lsize=121500000KiB time=00:12:00.00 bitrate=1382400.0kbits/s speed=11.4x elapsed=0:01:03.42


    D:\tools\ffmpeg\ffmpeg-n9.0\bin>fp ffmpeg -i "C:\Users\SoraYuki\Videos\2025-09-24 00-10-28.mp4" -t 600 -f rawvideo -y - "|" ffmpeg -f rawvideo -pix_fmt yuv420p10le -s 1920x1200 -i - -f rawvideo -y nul
    (...)
    frame=18000 fps=417 q=-0.0 Lsize=121500000KiB time=00:12:00.00 bitrate=1382400.0kbits/s speed=16.7x elapsed=0:00:43.14

# Internal
## Pipe with large buffer
It calls CreatePipe to create a pair of anonymous pipes with large buffers, and then passes the pipe handles to both producer and consumer processes to connect them. The default size is 16 MB.

## stdio with large buffer
It hooks the C runtime function ```_setmode``` on stdio and calls ```setvbuf``` with a larger buffer when it switched to binary mode.
