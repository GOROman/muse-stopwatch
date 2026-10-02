# Muse StopWatch — Focus Buddy

M5Stack StopWatch **C152** 用の、オフラインでも動く集中タイマー＋Muse Gadget 実験ポート。

> **実験段階 / hardware unverified.** タイマーのホストテストは成功。ESP-IDF v6.0.1 の導入を試みましたが、ネットワーク／実行承認の制限で完了できなかったため、ESP32 ファームウェアのコンパイル・書き込み・実機動作は未検証です。完成済み製品ではありません。

## できること

- 初期値25分の集中タイマー。スタート／一時停止／再開、リセット、ラップ回数
- 0秒に設定すると通常のストップウォッチ、最大24時間のカウントダウン
- C152 の AMOLED に大きな時間表示と接続状態
- Muse SDK の BLE ペアリング・暗号化セッションを継承し、`focus.*` コマンドで状態取得・操作
- タイマーはクラウド応答を待たずローカルで動作。マイク録音は実装していません

## 制約

音声会話、タッチ、通知音、振動、バッテリー残量表示、自動スリープ、永続化、ラップ時刻の履歴は未実装。ラップは回数のみ。再起動するとタイマーは初期値に戻ります。計測は起動中の単調時計に基づき、医療・競技計測用途ではありません。

Museとの接続には利用対象アカウントと SDK token が必要です。2026年10月時点、日本での提供時期は未定とされています。[Meta 日本語発表](https://about.fb.com/ja/news/2026/09/introducing-muse-personal-ai-agent/amp/) を確認してください。日本への帰国後の接続可否は保証できません。アカウントなしでもローカルタイマーの使用を意図していますが、実機未検証です。

## 操作

- A（GPIO2）: スタート／一時停止／再開
- B（GPIO1）: ラップ。約1.5秒ホールドでタイマーのリセット
- BLE ペアリング確認時: B を短く押す
- **B を5秒ホールドすると、SDK の登録解除／初期化操作になります。通常リセットでは5秒まで押し続けないでください。**
- カウントダウン完了後はリセットしてから開始

ボタンの色や配置は実機で確認してください。GPIO0は通常操作に使いません。

## ビルド

macOS/Linux、Python 3、Git、Cコンパイラ、[ESP-IDF v6.0.1](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/get-started/index.html) が必要です。M5Stack 工場デモの v5.5.4 とは異なります。

```sh
# ESP-IDF の公式インストール手順に従い、esp32s3 ツールをインストール
. /path/to/esp-idf/export.sh
./tools/test.sh
python3 tools/prepare.py
./tools/build.sh menuconfig
./tools/build.sh build
```

menuconfig の「ESP32 Device SDK > Muse Gadgets SDK token」に、[Muse Gadgets](https://gadgets.muse.ai/settings/sdk-tokens) で本人が発行した token をローカル入力してください。token をチャット・ソース・スクリーンショットへ載せないでください。使用前に [Gadget SDK Terms](https://gadgets.muse.ai/sdk-terms) を確認してください。未入力でも上流SDKは警告付きビルドを許しますが、Museとのペアリングは利用できない場合があります。

SDK はコミット `7e7123e2815d3e7e3c0f2ca330f576290ae6a6a9` に固定。`.build-sdk/` に取得し、少数の overlay と検証付きパッチを適用します。既存ディレクトリは上書きしません。再準備時は設定を安全にバックアップし、別の作業コピーを使用してください。管理コンポーネントは上流のバージョン範囲で解決されるため、完全な bit-for-bit reproducible build ではありません。成功したビルドの dependencies.lock を保存すると依存解決の再現性が上がります。

## 書き込みとペアリング（本人の実機で実施）

書き込みは現在のファームウェアを置き換えます。先にM5Stack公式の復旧手順・工場ファームウェアを準備してください。`erase-flash` や eFuse 書き込みはこのプロジェクトの通常セットアップには不要です。

```sh
./tools/build.sh -p /dev/ttyACM0 flash monitor
# macOS の例: /dev/cu.usbmodemXXXX
# シリアルモニタ終了: Ctrl-]
```

起動・画面・ボタン・タイマーを先に確認し、その後Museアプリで Settings → Devices → Developer mode を有効にしてデバイス追加。要求された時だけ B を短押しして確認します。信頼できるネットワーク上でセットアップしてください。

## Museからのコマンド

このポートは SDK の既存の認証済み Noise control dispatcher を拡張します。公開HTTPサーバーは追加していません。自然言語での自動発見・ツール登録は同梱していないため、Muse側の利用可能な gadget command 経路から次のコマンドを送る連携が別途必要です。

| command | params | effect |
|---|---|---|
| `focus.status` | `{}` | 状態取得 |
| `focus.toggle` | `{}` | 開始／一時停止／再開 |
| `focus.reset` | `{}` | 設定時間を保持してリセット |
| `focus.lap` | `{}` | 動作中だけラップ回数+1 |
| `focus.configure` | `{"seconds":1500}` | 0〜86400の整数で設定しリセット |

成功時は `{ "ok": true, "payload": {...} }`。payload の内容: elapsed_ms, remaining_ms, target_ms, laps, running, complete。0秒は無期限計測。設定変更は既存セッションをリセットします。`toggle` や `lap` は冪等ではないため通信再送で重複実行しないでください。内部エラーは `{ "ok": false, "error": { "code": "invalid_params", "message": "..." } }`、通信上はSDKが error.message を文字列に変換します。未知の名前空間はSDK側へ渡します。

追加の通信コマンドテストは、公式 cJSON v1.7.19 のソースを用意し `CJSON_SOURCE=/path/to/cJSON ./tools/test-commands.sh` で実行できます。CIはホストテストのみです。

## セキュリティ

- token、Wi-Fi資格情報、ビルド済みバイナリ、sdkconfig、秘密鍵はコミットしないでください。`.gitignore` に対象を含めています
- SDK token はファームウェアに埋め込まれます。公開バイナリは共有しないでください
- ペアリングは物理ボタン確認を伴いますが、コミュニティデバイスには製造元証明がなく、能動的MITMへの保証はありません
- OTA、ホームネットワークトンネル、サポートログ送信は本ポートで無効化しています
- NVS暗号化は既定では有効化しません。物理アクセスがあれば保存済み資格情報を抽出される可能性があります。上流の暗号化設定はeFuse鍵生成を伴い得るため、理解した上で本人が選択してください
- 上流SDKが同梱する公開の開発用署名鍵はセキュリティ保証になりません。本リポジトリにはコピーしていません。Secure Boot/eFuseを自動設定しません

## English

An experimental C152 port of Muse Gadget SDK with a local focus countdown and stopwatch. The small overlay adds an AMOLED status UI, A/B button controls, a thread-safe timer, and `focus.*` commands through the existing authenticated SDK dispatcher. No microphone recording, voice assistant, touch input, persistence or low-power lifecycle is implemented.

Use ESP-IDF **6.0.1**, run `./tools/test.sh`, `python3 tools/prepare.py`, then `./tools/build.sh menuconfig` and `./tools/build.sh build`. Enter your own token locally; never publish generated configuration or firmware. See the Japanese sections above for exact commands. The firmware is **not compile-verified or hardware-tested** in this environment; only host tests pass. Pairing requires a supported Muse account and regional service availability, not just a successful build. Local timer operation is designed to be independent of Muse connectivity. B confirms pairing; holding B for five seconds invokes the upstream reset/unpair action.

## Sources & licenses

Our additions: MIT, see [LICENSE](LICENSE). Hardware-derived code retains M5Stack attribution. [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists upstream sources and licenses. See [hardware notes](docs/hardware.md) and [validation record](docs/validation.md).
