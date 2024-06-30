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
```  
sudo apt-get install libsfml-dev
```    
  
### セットアップ
Mp4動画をセットし、動画ファイル名を必要に応じて変更する。sh cmake.shでビルドを行い、sh run.shで実行する。  
```c
//11行目～14行目
const int HEIGHT = 40; //動画の大きさ調整
const float volume = 80.0f; //音声の音量調整
const float speed = 0.970f; //音声再生スピード調整
const std::string FILENAME = "bad_apple.mp4"; // 動画ファイル名
```
