with open('/tmp/ffmpeg-7.1/libavcodec/wmaprodec.c', 'r') as f:
    lines = f.readlines()
lines = [l for l in lines if 'libavutil/internal.h' not in l]
lines.insert(89, '#include "libavutil/internal.h"\n')
with open('/tmp/ffmpeg-7.1/libavcodec/wmaprodec.c', 'w') as f:
    f.writelines(lines)
print('Updated wmaprodec.c successfully')
