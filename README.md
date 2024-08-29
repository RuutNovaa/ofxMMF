ofxMMF is an openframeworks addon for Microsoft Media Foundation. This is still a bit of a draft. Apologies for the dirty code :). I found Openframeworks ofVideoGrabber is extremely slow for 4k camera's, to my suprise the Windows Camera preview was doing fine on high resolutions. ofxMMF easily grabs 4K video at 30FPS.

Currently ofxMMF is setup to grab NV12 encoded video streams instead of YUY2 or RGB24. Its quite easy to adapt the code for grabbing differently encoded streams.

Usage:

.h

```
ofxMMF* _c;
```
.cpp

```
setup:
  _c = new ofxMMF();
  _c->CreateCaptureDevice(3840,2160);

update:
  _c->update();

draw:
  _c->camTex.draw(0, 50);


```

For changing the video encoding look in the ```setSourceReader function for nativeGuid == MFVideoFormat_NV12 ``` note that then you also would need to adapt the update function to copy the set encoding type to RGB24 for openframeworks.

Credits to Andre Chen for the Yuv2rgb conversion https://github.com/andrechen/yuv2rgb/tree/master

