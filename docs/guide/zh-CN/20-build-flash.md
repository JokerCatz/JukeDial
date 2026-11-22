# 20. 从源码构建与刷机

这一章给想自己构建固件的人。只想刷发布的镜像，用网页工具的刷机分页就好（第 13 章）。

## 前置需求
- **ESP-IDF v6.1。** ESP32-S31 目前是 IDF 的预览目标，所以所有 `idf.py` 都要带 `--preview`（Makefile 已经帮你带）。Makefile 预期用 `$HOME/.espressif/tools/activate_idf_v6.1.sh` 启用 IDF（用 Espressif 的安装程序装 v6.1 就会有），IDF 本体默认在 `$HOME/.espressif/v6.1/esp-idf`（可用 `IDF_PATH` 改）。**不要**先自己 source 启用脚本再跑 make，Makefile 会自己 source。
- Python 3、CMake、C／C++ 编译器、`make`。
- 第一次构建要联网：IDF 组件管理器会依 `app/main/idf_component.yml` 与 `app/dependencies.lock` 把依赖组件下载到 `app/managed_components/`。
- 可选：模拟器需要 SDL2 与 FreeType 的开发文件（pkg-config 找得到）；生成卡片内容包、开机图与图标需要 Pillow；重建字体需要 fontTools 与 brotli（放进自己的 venv，不要装进 IDF 的环境）。

## 不在 git 里的东西
| 东西 | 怎么来 |
|---|---|
| 打过补丁的 Bluedroid（LDAC、aptX 用） | `make build` 与 `make build-fakepod` 会自动从你的 IDF 生成（`tools/bt_ldac/make_bt.sh`，放在 `.local/bt_ldac/`），不用手动做 |
| 支持 exFAT 的 FatFs | 每次构建前自动生成（`tools/fatfs_exfat/`） |
| `app/managed_components/` | 第一次构建时自动下载 |
| 字体分区镜像 `app/build/fonts.bin` | 需要 `fonts/ui-tc.ttf`、`ui-sc.ttf`、`ui-ext.ttf`、`ui-icon.ttf` 四个文件，再跑 `make fontpak`（`make flash-fonts` 会自动跑）。发布的合并镜像已经含字体，只有自己构建时才需要这一步。自己重建：`make fonts-all` 用 `fonts-src/` 里的 Noto 与图标原始文件生成四个字体（细节与补缺字：`docs/dev/fonts.md`） |
| 卡片内容包 | 发布的是 `jukedial-<版本>-card.zip`；自己构建用 `make card-zip`（HUD 表盘 `faces.zip` 由 `make faces-zip` 生成，需要 Pillow、Noto CJK 与 IBM Plex Sans Condensed）；解压缩到卡的根目录 |
| 白噪音包 | `make noise-pack`（纯 Python，可重现），`make card-zip` 会放进卡片包 |

没有字体分区也能开机，但字体会退回内置字体，中文字与图标可能缺字；要完整的界面请刷字体镜像。

## 构建
```
make build            # 圆形板（app/build）
make build-fakepod    # FakePod（app/build-fakepod）
make size             # 看内存使用（FakePod：make size-fakepod）
```
`make build-stock` 与 `make build-fakepod-stock` 是不含 LDAC／aptX 的比较用镜像，一般不需要。

## 第一次刷圆形板
1. 接上板子，在 `.local/board.mk` 设置端口：`PORT = /dev/serial/by-id/<你的板子>`（范例见 `.local/board.mk.example`）。一定要用 `/dev/serial/by-id/` 开头的路径，Makefile 不接受 `ttyACM0` 这种会变动的名字。
2. **先备份：** `make backup`，把整颗 16 MB flash 存到 `backup/factory_full_*.bin`。Makefile 在没有备份时拒绝所有刷机目标（`make flash`、`make flash-all`、`make flash-fonts`）。
3. `make flash-all`：写 bootloader、分区表与 app（会改写分区表，所以备份很重要）。
4. `make flash-fonts`：写分区表与字体分区。
5. 把卡片内容包解压缩到 TF 卡根目录，插卡。
之后只改程序时用 `make flash`（只写 app）。连不上时进下载模式：按住 USB-C 旁边的 BOOT 针孔再插线（第 15 章）。

## 刷 FakePod
Makefile 没有 FakePod 的刷机目标，用 esptool 手动写。FakePod 只有一个 USB 口，两个键同时按住再上电进下载模式。先备份：

```
python -m esptool --port <端口> read-flash 0 0x1000000 backup/fakepod_full.bin
```

这些指令请在 IDF 环境里执行（`source $HOME/.espressif/tools/activate_idf_v6.1.sh`）。`make build-fakepod` 与 `make fontpak` 之后，在 `app/build-fakepod/` 里：

```
python -m esptool --chip esp32s31 --port <端口> --before default-reset --after watchdog-reset \
  write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x2000   bootloader/bootloader.bin \
  0x8000   partition_table/partition-table.bin \
  0x10000  jc_player.bin \
  0xC00000 ../build/fonts.bin
```

| 地址 | 文件 | 内容 |
|---|---|---|
| 0x2000 | `bootloader/bootloader.bin` | bootloader（ESP32-S31 的 bootloader 在 0x2000，不是 0） |
| 0x8000 | `partition_table/partition-table.bin` | 分区表（`app/partitions.csv`） |
| 0x10000 | `jc_player.bin` | 应用程序（分区大小 0xBF0000） |
| 0xC00000 | `fonts.bin` | 字体分区（最后 4 MB） |

设置与配对数据在 0x9000 的 nvs 分区，上面的指令不会动它。结尾一定要 `--after watchdog-reset`：一般的复位会停在下载模式。发布的镜像是把这四块合并成一个文件、从地址 0 写入（第 13 章）。

## 模拟器与测试
- `make sim-run`（圆形）与 `make sim-run-fp`（FakePod）打开桌面模拟器窗口。模拟器要先跑过一次 `make build`（需要 `app/sdkconfig`）。无窗口与截图的用法见 `sim/README.md`。
- `make test`：主机端单元测试（`FAST=1 make test` 较快）。`make sim-test` 与 `make sim-test-fp`：用模拟器跑画面与行为测试。`make lint`：静态检查。
- 本导览的图片是用 `tools/simtools/guide_shots.py` 从模拟器生成的（`docs/guide/shotlist.md` 有清单）。

上一章：[FakePod 专属操作](19-fakepod.md)。回到[首次开机](01-first-boot.md)。
