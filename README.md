# BadApple  
動画を文字で再生するものです。C++ OpenCVを使用しています。  
Ubuntu環境で作成  
  
## インストールするもの
```  
sudo apt install build-essential cmake ninja-build libopencv-dev ffmpeg libsfml-dev
```   
  
### セットアップ
動画をセットし、動画ファイル名を必要に応じて変更する。sh cmake.shでビルドを行い、sh run.shで実行する。  
```c
const float volume = 10.0f;
const float speed = 1.0f;
const int fps_value = 3;
const int HEIGHT = 230;
const std::string FILENAME = "bad_apple.mp4"; // 動画ファイル名
```
