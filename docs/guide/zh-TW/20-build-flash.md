# 20. 從原始碼建置與刷機

這一章給想自己建置韌體的人。只想刷發佈的映像，用網頁工具的刷機分頁就好（第 13 章）。

## 前置需求
- **ESP-IDF v6.1。** ESP32-S31 目前是 IDF 的預覽目標，所以所有 `idf.py` 都要帶 `--preview`（Makefile 已經幫你帶）。Makefile 預期用 `$HOME/.espressif/tools/activate_idf_v6.1.sh` 啟用 IDF（用 Espressif 的安裝程式裝 v6.1 就會有），IDF 本體預設在 `$HOME/.espressif/v6.1/esp-idf`（可用 `IDF_PATH` 改）。**不要**先自己 source 啟用腳本再跑 make，Makefile 會自己 source。
- Python 3、CMake、C／C++ 編譯器、`make`。
- 第一次建置要網路：IDF 元件管理員會依 `app/main/idf_component.yml` 與 `app/dependencies.lock` 把相依元件下載到 `app/managed_components/`。
- 選用：模擬器需要 SDL2 與 FreeType 的開發檔（pkg-config 找得到）；產生卡片內容包、開機圖與圖示需要 Pillow；重建字型需要 fontTools 與 brotli（放進自己的 venv，不要裝進 IDF 的環境）。

## 不在 git 裡的東西
| 東西 | 怎麼來 |
|---|---|
| 修補過的 Bluedroid（LDAC、aptX 用） | `make build` 與 `make build-fakepod` 會自動從你的 IDF 產生（`tools/bt_ldac/make_bt.sh`，放在 `.local/bt_ldac/`），不用手動做 |
| 支援 exFAT 的 FatFs | 每次建置前自動產生（`tools/fatfs_exfat/`） |
| `app/managed_components/` | 第一次建置時自動下載 |
| 字型分割區映像 `app/build/fonts.bin` | 需要 `fonts/ui-tc.ttf`、`ui-sc.ttf`、`ui-ext.ttf`、`ui-icon.ttf` 四個檔，再跑 `make fontpak`（`make flash-fonts` 會自動跑）。發佈的合併映像已經含字型，只有自己建置時才需要這一步。自己重建：`make fonts-all` 用 `fonts-src/` 裡的 Noto 與圖示原始檔產生四個字型（細節與補缺字：`docs/dev/fonts.md`） |
| 卡片內容包 | 發佈的是 `jukedial-<版本>-card.zip`；自己建置用 `make card-zip`（HUD 錶面 `faces.zip` 由 `make faces-zip` 產生，需要 Pillow、Noto CJK 與 IBM Plex Sans Condensed）；解壓縮到卡的根目錄 |
| 白噪音包 | `make noise-pack`（純 Python，可重現），`make card-zip` 會放進卡片包 |

沒有字型分割區也能開機，但字型會退回內建字型，中文字與圖示可能缺字；要完整的介面請刷字型映像。

## 建置
```
make build            # 圓形板（app/build）
make build-fakepod    # FakePod（app/build-fakepod）
make size             # 看記憶體使用（FakePod：make size-fakepod）
```
`make build-stock` 與 `make build-fakepod-stock` 是不含 LDAC／aptX 的比較用映像，一般不需要。

## 第一次刷圓形板
1. 接上板子，在 `.local/board.mk` 設定埠：`PORT = /dev/serial/by-id/<你的板子>`（範例見 `.local/board.mk.example`）。一定要用 `/dev/serial/by-id/` 開頭的路徑，Makefile 不接受 `ttyACM0` 這種會變動的名字。
2. **先備份：** `make backup`，把整顆 16 MB flash 存到 `backup/factory_full_*.bin`。Makefile 在沒有備份時拒絕所有刷機目標（`make flash`、`make flash-all`、`make flash-fonts`）。
3. `make flash-all`：寫 bootloader、分割表與 app（會改寫分割表，所以備份很重要）。
4. `make flash-fonts`：寫分割表與字型分割區。
5. 把卡片內容包解壓縮到 TF 卡根目錄，插卡。
之後只改程式時用 `make flash`（只寫 app）。連不上時進下載模式：按住 USB-C 旁邊的 BOOT 針孔再插線（第 15 章）。

## 刷 FakePod
Makefile 沒有 FakePod 的刷機目標，用 esptool 手動寫。FakePod 只有一個 USB 埠，兩顆鍵同時按住再通電進下載模式。先備份：

```
python -m esptool --port <埠> read-flash 0 0x1000000 backup/fakepod_full.bin
```

這些指令請在 IDF 環境裡執行（`source $HOME/.espressif/tools/activate_idf_v6.1.sh`）。`make build-fakepod` 與 `make fontpak` 之後，在 `app/build-fakepod/` 裡：

```
python -m esptool --chip esp32s31 --port <埠> --before default-reset --after watchdog-reset \
  write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x2000   bootloader/bootloader.bin \
  0x8000   partition_table/partition-table.bin \
  0x10000  jc_player.bin \
  0xC00000 ../build/fonts.bin
```

| 位址 | 檔案 | 內容 |
|---|---|---|
| 0x2000 | `bootloader/bootloader.bin` | bootloader（ESP32-S31 的 bootloader 在 0x2000，不是 0） |
| 0x8000 | `partition_table/partition-table.bin` | 分割表（`app/partitions.csv`） |
| 0x10000 | `jc_player.bin` | 應用程式（分割區大小 0xBF0000） |
| 0xC00000 | `fonts.bin` | 字型分割區（最後 4 MB） |

設定與配對資料在 0x9000 的 nvs 分割區，上面的指令不會動它。結尾一定要 `--after watchdog-reset`：一般的重設會停在下載模式。發佈的映像是把這四塊合併成一個檔案、從位址 0 寫入（第 13 章）。

## 模擬器與測試
- `make sim-run`（圓形）與 `make sim-run-fp`（FakePod）開桌面模擬器視窗。模擬器要先跑過一次 `make build`（需要 `app/sdkconfig`）。無視窗與截圖的用法見 `sim/README.md`。
- `make test`：主機端單元測試（`FAST=1 make test` 較快）。`make sim-test` 與 `make sim-test-fp`：用模擬器跑畫面與行為測試。`make lint`：靜態檢查。
- 本導覽的圖片是用 `tools/simtools/guide_shots.py` 從模擬器產生的（`docs/guide/shotlist.md` 有清單）。

上一章：[FakePod 專屬操作](19-fakepod.md)。回到[第一次開機](01-first-boot.md)。
