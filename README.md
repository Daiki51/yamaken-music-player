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

## ライセンス

[MIT](https://github.com/atom/atom/blob/master/LICENSE.md)
