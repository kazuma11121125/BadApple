# BadApple  
動画を文字で再生するものです。C++ OpenCVを使用しています。  
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
```  
sudo apt install ffmpeg
```  
  
### セットアップ
Mp4動画をセットし、動画ファイル名を必要に応じて変更する。sh cmake.shでビルドを行い、sh run.shで実行する。  
```c
const int HEIGHT = 40; // 10行目 描画高さの調整。
const std::string FILENAME = "bad_apple.mp4"; // 11行目 動画ファイル名
```
