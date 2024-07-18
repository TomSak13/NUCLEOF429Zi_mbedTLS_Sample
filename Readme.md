NUCLEOF429Zi mbed-TLS Sample Source
======================================

このリポジトリは、下記の記事で使用したサンプルソースコードです。
リポジトリのソースコードを使用する際は必ずこちらを先にお読みください。


また、このソースコードを使用したことによる動作保証はしておりません。あくまで参考としてご使用ください。


## フォルダ構成

- Inc
  - RNGWrapper.h
  - SSLClient.h
- Src
  - RNGWrapper.c
  - SSLClient.c

RNGWrapper.c/hは、STM32CubeMXで用意されている乱数生成モジュール(RNG)デバイスドライバのラッパーです。  
RNGの初期化とクロック設定、mbed-TLSで使用するコールバックの定義をしています。  
  
SSLClient.c/hは、SSL/TLSを使用してHTTP GETのリクエストを送るサンプルソースコードです。こちらもSTM32CubeMXで用意されているRTOS環境で動作します。
「void SSLClient(void const * argument)」関数をスレッド(タスク)として呼び出すことで、動作します。

