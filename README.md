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

## ブランチ
main・・・白黒版  
clolr ・・・カラー版

## コード設定
```c 
//白黒版 12行目～15行目
const int HEIGHT = 40; //動画の大きさ調整
const float volume = 80.0f; //音声の音量調整
const float speed = 1.0f; //音声再生スピード調整
const std::string FILENAME = "bad_apple.mp4"; // 動画ファイル名

//カラー版 12行目〜16行目
const float volume = 80.0f;//動画の大きさ調整
const float speed = 1.0f; //音声再生スピード調整
const int fps_value = 3; //フレームの削減割合
const int HEIGHT = 81; // 画像の高さ
const std::string FILENAME = "idol.webm"; // 動画ファイル名
```

## 設定補足
推奨OS:Ubuntu  
kazuma1112実行環境   
CPU | core i5 1235U  
RAM | 16GB  
OS | Ubuntu 22.04 LTS  
  
HEIGHT 40〜100範囲  
白黒版FPS MAX 60fpsまで。  
カラー版PFS MAX 10fps(fps_valueで30fps→10fpsみたいにしているため30fpsまでセット可能)。  



