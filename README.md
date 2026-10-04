# Abstract
Fastpipe is a tool that provides pipes with large buffers for inter-process communication. It offers better performance than simple ```|``` operator in the Windows command prompt.

# Usage example
    fp ffmpeg -i "input.mp4" -f yuv4mpegpipe - "|" ffmpeg -f yuv4mpegpipe -i - -c libx264 output.mp4

use ```"|"``` instead of ```|```. You can set environment variable "FP_BUFFERSIZE" specifies the buffer size in bytes.

# Internal
## Pipe with large buffer
It calls CreatePipe to create a pair of anonymous pipes with large buffers, and then passes the pipe handles to both producer and consumer processes to connect them. The default size is 16 MB.

## stdio with large buffer
It hooks the C runtime function ```_setmode``` on stdio and calls ```setvbuf``` with a larger buffer when it switched to binary mode.
