# BadApple  
動画を文字で再生するものです。C++ OpenCVを利用しています。  
Ubuntu環境で作成  
  
## インストールするもの
```  
sudo apt install build-essential
```   
```  
sudo apt install cmake
```  
```  
sudo apt-get install ninja-build  
```  
```  
sudo apt install libopencv-dev  
```  

### セットアップ
Mp4動画をセットする。framesのフォルダを作成。sh cmake.shでビルドを行い、sh run.shで実行する。  
```c
const int CLIP_FRAMES = 6571;// 10行目 動画のフレーム数を入力
const int WIDTH = 100;// 12行目 再生サイズの調整

cv::VideoCapture vidObj("bad_apple.mp4");// 49行目 動画ファイル名をセット

usleep(130000);//101行目 フレーム間隔を調整
```
