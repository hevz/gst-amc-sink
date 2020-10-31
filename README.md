# GstAmcSink

GstAmcSink is an Android media codec video sink for GStreamer 1.0.

**Features**
* Decode and render directly, zero copy.
* Various encoding format support (need hardware support):
  * H.263
  * H.264
  * H.265
  * MPEGv1
  * MPEGv2
  * MPEGv4
  * VP8
  * VP9

## How to Build

```bash
git clone git://github.com/heiher/gst-amc-sink
cd gst-amc-sink
nkd-build
```

## Pipeline

```
playbin video-sink=amcvideosink

udpsrc ! tsdemux ! h264parse ! amcvideosink
```

## Authors
* **Heiher** - https://hev.cc

## License
LGPL
