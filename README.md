# YamakenMusicPlayer

「自動昼休み音楽プレイヤー(yamaken-music-player)」は毎日同じ時刻に音楽を再生するプレイヤーです。

![YamakenMusicPlayer](/doc/images/top.png)

GitHub: [https://github.com/Daiki51/yamaken-music-player](https://github.com/Daiki51/yamaken-music-player)

## 特徴
- 毎日同じ時刻に指定した時間(分)だけ音楽を再生
- 曲順は毎日シャッフル
- 音楽ファイルはSDカードから読み出し
- 手動での再生も対応
- NTPで時刻を同期しているため、時刻調整が不要
- IFTTTのサービスを利用し、音楽再生時にユーザーに通知可能
- Arduino IDEに対応したチップを使用しているため、振る舞いを簡単にカスタマイズ可

## ドキュメンテーション

[再生する曲の変更]()
[制作方法]()

 \
基板: [board.pdf](/circuit/YamakenMusicPlayer/board.pdf)

## 回路図

詳細: [schematic.pdf](/circuit/YamakenMusicPlayer/schematic.pdf)

### A 電源回路部
![Schematic A](/doc/images/schematic_a.png)

### B 制御IC部
![Schematic B](/doc/images/schematic_b.png)

### C プレイヤーIC部
![Schematic C](/doc/images/schematic_c.png)

# 部品表

| 記号       | 品名                   | 個数 | 備考                                                |
|------------|------------------------|------|-----------------------------------------------------|
| U1         | NJM2396F33             | 1    | 定電圧リニアレギュレータ                              |
| U2         | AE-ESP-WROOM-02        | 1    | Wi-Fiモジュール(制御ICとして利用)                     |
| U3         | DFR0299(DFPlayer Mini) | 1    | MP3モジュール                                        |
| J1         | MJ-179PH                | 1    | 2.1mm標準DCジャック                                 |
| J2         | AJ-1780                | 1    | 3.5mmステレオミニジャック                             |
| R1, R3, R4 | 10kΩ                   | 3    | 1/4W 炭素皮膜抵抗　表示「茶黒橙金」                    |
| R2         | 220Ω                   | 1    | 1/4W 炭素皮膜抵抗　表示「赤赤茶金」                    |
| R5         | 100Ω                   | 1    | 1/4W 炭素皮膜抵抗　表示「茶黒茶金」                    |
| C1         | 0.33μF                 | 1    | セラミックコンデンサ                                  |
| C2         | 22μF                   | 1    | 電解コンデンサ                                       |
| C3, C5, C7 | 0.1μF                  | 3    | セラミックコンデンサ                                  |
| C4, C6     | 10μF                   | 2    | セラミックコンデンサ                                 |
| S1           | DIPスイッチ            | 1    | ブートモード切り替えスイッチ                          |
| S2         | タクトスイッチ(赤)     | 1    | リセットボタン                                        |
| S3         | タクトスイッチ(白)     | 1    | 再生ボタン                                            |
| LED1       | 緑色LED                | 1    | 抵抗内蔵LED                                         |
| LED2       | 青色LED                | 1    | 抵抗内蔵LED                                         |
| JP1, JP2   | ピンヘッダ(3ピン)       | 2    | 2.54mmピッチ シングルライン                          |
|            | SH16K4B102L20KC        | 1    | 小型ボリューム 1kΩB                                  |
|            | ABS-15                | 1    | 小型ボリューム用ツマミ                                |

## ライセンス

[MIT](https://github.com/atom/atom/blob/master/LICENSE.md)
