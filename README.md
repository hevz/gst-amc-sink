# GstAmcSink

GstAmcSink is an Android media codec video sink for GStreamer 1.0.

**Features**
* Decode H.264 byte-stream and Render.

## How to Build

```bash
git clone git://github.com/heiher/gst-amc-sink
cd gst-amc-sink
nkd-build
```

## Pipeline

```
udpsrc ! tsdemux ! h264parse ! amcvideosink
```

## Authors
* **Heiher** - https://hev.cc

## License
LGPL

