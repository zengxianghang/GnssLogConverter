# pos_convert — GNSS/INS 文本格式转换工具

`pos_convert` 是一个面向 GNSS/INS 数据处理的 Windows 命令行工具。程序使用单个可执行文件 `pos_convert.exe` 提供多种格式转换能力，当前主要用于自定义定位结果、NovAtel OEM7 `INSPVA` / `INSPVAX` 与 `BESTPOS` / NMEA GGA 之间的转换。

程序采用流式读取方式处理文件，不需要把整个日志一次性加载到内存；同时支持按整数倍降频输出，并对新生成的 NMEA/NovAtel ASCII 消息重新计算校验码。

> 当前版本一共支持 4 种转换模式：
>
> 1. 自定义定位结果 → `GGA + INSPVA + BESTPOS`
> 2. `INSPVA` → `BESTPOS`
> 3. `INSPVAX` → `INSPVA`
> 4. `INSPVAX` → `BESTPOS`

---

## 1. 文件说明

发布包主要包含：

```text
pos_convert.cpp          C++ 源码
build_pos_convert.bat    Visual Studio 自动编译脚本
README.md                本说明文档
```

编译后得到：

```text
pos_convert.exe
```

---

## 2. 支持的转换模式

| 模式 | 输入 | 输出 | 主要用途 |
|---|---|---|---|
| 1 | 自定义定位结果文本 | `.gga` + `.inspva` + `.bestpos` | 将已有定位/速度/姿态结果模拟成常用 GNSS/INS 日志 |
| 2 | NovAtel INSPVA ASCII | BESTPOS ASCII | 从组合导航结果生成位置日志 |
| 3 | NovAtel INSPVAX ASCII | INSPVA ASCII | 将扩展 INS 日志简化为 INSPVA |
| 4 | NovAtel INSPVAX ASCII | BESTPOS ASCII | 从 INSPVAX 中直接提取 BESTPOS 所需位置/精度信息 |

所有新生成的 NovAtel ASCII 消息都会重新计算 CRC32。

---

# 3. 编译

## 3.1 推荐：使用自动 Visual Studio 编译脚本

电脑已经安装 Visual Studio 时，将下面两个文件放在同一个目录：

```text
pos_convert.cpp
build_pos_convert.bat
```

直接双击：

```bat
build_pos_convert.bat
```

脚本会自动完成：

1. 检查当前命令行环境是否已经存在 `cl.exe`；
2. 优先调用 Visual Studio 官方的 `vswhere.exe` 查找安装目录；
3. 如果 `vswhere.exe` 不可用，再尝试 Visual Studio 2022 / 2019 / 2017 的常见安装路径；
4. 调用 `VC\Auxiliary\Build\vcvars64.bat` 初始化 x64 MSVC 环境；
5. 使用 `cl.exe` 编译 `pos_convert.cpp`；
6. 生成 `pos_convert.exe`。

默认编译参数为：

```text
/O2
/EHsc
/W4
/std:c++14
/utf-8
/D_CRT_SECURE_NO_WARNINGS
```

其中 `/O2` 开启速度优化，生成的是 x64 可执行文件。

### 命令行方式

默认编译：

```bat
build_pos_convert.bat
```

指定源码：

```bat
build_pos_convert.bat my_source.cpp
```

指定源码和输出 EXE：

```bat
build_pos_convert.bat my_source.cpp my_tool.exe
```

自动化脚本中不希望最后 `pause`：

```bat
build_pos_convert.bat pos_convert.cpp pos_convert.exe nopause
```

如果提示找不到 C++ 编译器，请打开 **Visual Studio Installer** 并安装：

```text
Desktop development with C++
```

---

# 4. 查看帮助

以下两种方式等价：

```bat
pos_convert.exe -h
```

```bat
pos_convert.exe --help
```

即使已经写了转换模式，也可以使用帮助选项，例如：

```bat
pos_convert.exe --inspvax-to-bestpos -h
```

程序会打印所有模式、参数和常见示例。

---

# 5. 模式 1：自定义定位结果 → GGA + INSPVA + BESTPOS

## 5.1 命令格式

```bat
pos_convert.exe input.txt output_prefix [options]
```

例如：

```bat
pos_convert.exe input.txt result --week 2362
```

生成：

```text
result.gga
result.inspva
result.bestpos
```

## 5.2 支持的输入格式

程序支持输入行 **包含 Week** 或 **不包含 Week** 两种形式。

### 格式 A：包含 Week

```text
Week UTCSecond UTCHMS Latitude Longitude H-Ell VX-ECEF VY-ECEF VZ-ECEF Q Omega Phi Heading Roll Pitch SDHeight SDHoriz ...
```

示例：

```text
2362.00000 99000.00 3:30:00.00 22.6904910300 113.9863519239 89.807 17.834 10.649 -5.300 1 0.0 0.0 -105.78 -1.81 0.57 1.1 0.6
```

### 格式 B：不包含 Week

```text
UTCSecond UTCHMS Latitude Longitude H-Ell VX-ECEF VY-ECEF VZ-ECEF Q Omega Phi Heading Roll Pitch SDHeight SDHoriz ...
```

示例：

```text
204600.00 8:50:00.00 22.8318169856 114.9397454358 89.807 17.834 10.649 -5.300 1 0.0 0.0 -105.78 -1.81 0.57 1.1 0.6
```

没有 Week 时，GGA 仍然可以输出，因为 GGA 只需要 UTC 日内时刻；但 INSPVA/BESTPOS 需要 GNSS week，因此必须使用：

```bat
--week 2362
```

## 5.3 字段使用关系

程序实际使用的字段包括：

| 输入字段 | 用途 |
|---|---|
| Week | 生成 NovAtel GNSS 时间 |
| UTCSecond | UTC 周内秒；转换为 GNSS 周内秒 |
| UTCHMS | 生成 GGA 的 `hhmmss.ss` |
| Latitude / Longitude | GGA、INSPVA、BESTPOS |
| H-Ell | 椭球高；INSPVA 直接使用 |
| VX/VY/VZ-ECEF | 转换为 North/East/Up 速度后写入 INSPVA |
| Q | 粗略映射有效/无效解状态 |
| Heading | 转换到 `[0,360)` 后写入 INSPVA azimuth |
| Roll / Pitch | 写入 INSPVA |
| SDHeight | BESTPOS 高度标准差，源单位按 cm 处理 |
| SDHoriz | BESTPOS 水平标准差，源单位按 cm 处理 |

`Omega`、`Phi` 当前只用于保持输入字段位置兼容，不参与输出计算。

## 5.4 GGA 时间处理

GGA 使用 UTC 时间。

程序优先解析输入中的：

```text
UTCHMS = H:MM:SS.ss
```

并输出为：

```text
hhmmss.ss
```

例如：

```text
8:50:00.10
```

转换为：

```text
085000.10
```

如果 HMS 字段无法正常解析，则回退到 `UTCSecond mod 86400` 计算日内时间。

## 5.5 GGA 经纬度转换

输入为十进制度：

```text
Latitude  = dd.dddddd
Longitude = ddd.dddddd
```

GGA 输出为：

```text
ddmm.mmmmmmmm,N/S
dddmm.mmmmmmmm,E/W
```

程序生成 `GNGGA`，即：

```text
$GNGGA,...*XX
```

GGA 校验码为 `$` 和 `*` 之间所有字符逐字节 XOR。

## 5.6 时间：UTC → GNSS

INSPVA/BESTPOS 的时间使用 GNSS week + GNSS seconds。

转换关系：

```text
GNSS second = UTC second + UTC offset
```

默认：

```text
UTC offset = 18 s
```

可使用：

```bat
--utc-offset 18
```

如果加偏移后跨越周边界，代码会自动增加或减少 GNSS week。

注意：`--utc-offset` 只用于模式 1。INSPVA/INSPVAX 本身已经携带 GNSS 时间，不再重复加 18 秒。

## 5.7 高度转换

输入 `H-Ell` 按 **椭球高** 处理。

GGA/BESTPOS 需要 MSL 高度时使用：

```text
MSL Height = Ellipsoidal Height - Geoid Undulation
```

因此可通过：

```bat
--geoid-sep -17.0
```

指定大地水准面起伏。

INSPVA 的高度继续使用原始椭球高，不做该减法。

## 5.8 ECEF 速度 → NEU 速度

源文件提供：

```text
VX-ECEF
VY-ECEF
VZ-ECEF
```

INSPVA 需要：

```text
North velocity
East velocity
Up velocity
```

程序根据当前位置的纬度 `lat` 和经度 `lon` 进行 ECEF→局部 NEU 旋转：

```text
Ve = -sin(lon)*Vx + cos(lon)*Vy

Vn = -sin(lat)*cos(lon)*Vx
     -sin(lat)*sin(lon)*Vy
     +cos(lat)*Vz

Vu =  cos(lat)*cos(lon)*Vx
     +cos(lat)*sin(lon)*Vy
     +sin(lat)*Vz
```

没有进行速度插值或平滑。

## 5.9 Q 状态映射

由于源文件只有简单的 `Q` 字段，无法恢复完整的 NovAtel 定位类型，因此当前采用简化映射：

```text
Q > 0
  GGA fix quality = 1
  INSPVA status    = INS_SOLUTION_GOOD
  BESTPOS status   = SOL_COMPUTED
  BESTPOS pos type = SINGLE

Q <= 0
  GGA fix quality = 0
  INSPVA status    = INS_INACTIVE
  BESTPOS status   = INSUFFICIENT_OBS
  BESTPOS pos type = NONE
```

这只是格式兼容映射，并不意味着原始数据实际一定是 NovAtel `SINGLE` 解。

## 5.10 BESTPOS 标准差

源格式的：

```text
SDHeight
SDHoriz
```

代码按照 **cm** 解释，然后转换到 BESTPOS 所需的 **m**：

```text
height sigma     = SDHeight * 0.01
latitude sigma   = SDHoriz  * 0.01
longitude sigma  = SDHoriz  * 0.01
```

如果源行没有这两个字段，则默认填 0。

## 5.11 常用示例

最简单：

```bat
pos_convert.exe input.txt result --week 2362
```

指定 geoid / 卫星数 / HDOP：

```bat
pos_convert.exe input.txt result ^
    --week 2362 ^
    --geoid-sep -17.0 ^
    --sat-num 30 ^
    --hdop 0.8
```

自动识别源频率并输出 10 Hz：

```bat
pos_convert.exe input.txt result --week 2362 --rate 10
```

明确指定 100 Hz → 10 Hz：

```bat
pos_convert.exe input.txt result --week 2362 --rate 100 10
```

---

# 6. 模式 2：INSPVA → BESTPOS

## 6.1 命令格式

```bat
pos_convert.exe --inspva-to-bestpos input.inspva output.bestpos [options]
```

最简单：

```bat
pos_convert.exe --inspva-to-bestpos input.inspva output.bestpos
```

## 6.2 支持的 INSPVA 类型

支持：

```text
#INSPVAA
%INSPVASA
```

即标准 ASCII 长头和 ASCII 短头形式。

输入中的 CRC 不作为转换前置条件；程序从 `*` 前解析数据，并为输出 BESTPOS 重新计算正确 CRC。

## 6.3 可直接获得的字段

INSPVA 可以直接提供：

```text
GNSS week
GNSS seconds
latitude
longitude
ellipsoidal height
INS status
```

因此时间和经纬度无需推算。

## 6.4 INSPVA 缺少的 BESTPOS 信息

INSPVA 本身没有完整的：

```text
position type
geoid undulation
latitude sigma
longitude sigma
height sigma
satellite count
```

所以需要通过命令参数补充。

### Position Type

默认：

```text
INS_PSRSP
```

可指定：

```bat
--pos-type INS_RTKFIXED
```

例如：

```bat
pos_convert.exe --inspva-to-bestpos input.inspva output.bestpos ^
    --pos-type INS_RTKFIXED
```

### Geoid Separation

```bat
--geoid-sep -17.0
```

转换：

```text
BESTPOS MSL height = INSPVA ellipsoidal height - geoid separation
```

### 标准差

```bat
--horiz-sigma 0.02
--height-sigma 0.04
```

其中水平标准差会同时写入 BESTPOS latitude sigma / longitude sigma。

### 卫星数

```bat
--sat-num 30
```

因为无法从 INSPVA 恢复实际卫星数，当前同一个数字会写入 BESTPOS 的多个卫星计数字段。

## 6.5 INS 状态映射

仅当：

```text
INS_SOLUTION_GOOD
```

时输出：

```text
SOL_COMPUTED,<指定的 position type>
```

其他 INS 状态输出：

```text
INSUFFICIENT_OBS,NONE
```

## 6.6 示例

普通转换：

```bat
pos_convert.exe --inspva-to-bestpos input.inspva output.bestpos
```

设置 geoid：

```bat
pos_convert.exe --inspva-to-bestpos input.inspva output.bestpos --geoid-sep -17.0
```

RTK Fixed：

```bat
pos_convert.exe --inspva-to-bestpos input.inspva output.bestpos --pos-type INS_RTKFIXED
```

补充精度和卫星数：

```bat
pos_convert.exe --inspva-to-bestpos input.inspva output.bestpos ^
    --pos-type INS_RTKFIXED ^
    --geoid-sep -17.0 ^
    --horiz-sigma 0.02 ^
    --height-sigma 0.04 ^
    --sat-num 30
```

自动降为 10 Hz：

```bat
pos_convert.exe --inspva-to-bestpos input.inspva output.bestpos --rate 10
```

---

# 7. 模式 3：INSPVAX → INSPVA

## 7.1 命令格式

```bat
pos_convert.exe --inspvax-to-inspva input.log output.log [options]
```

## 7.2 输入格式

当前 INSPVAX 解析器匹配：

```text
#INSPVAXA,
```

即 NovAtel ASCII 长头 INSPVAX。

程序至少要求：

- 存在 `;` 分隔 header 与 data；
- 存在 `*` 分隔数据和 CRC 文本；
- header 中能够读取 GNSS week / seconds；
- data area 至少具有所需 INSPVAX 字段。

**CRC 数值本身不校验。** 即使输入 INSPVAX 的 CRC 错误，只要记录结构完整，仍可转换。

## 7.3 CRC 行为

INSPVAX → INSPVA 的规则是：

```text
输入 INSPVAX CRC：忽略
输出 INSPVA CRC：重新计算
```

`--ignore-input-crc` 仅为兼容旧命令保留，因为当前实现始终忽略源 CRC。

## 7.4 高度转换

INSPVAX 提供：

```text
MSL height
undulation
```

INSPVA 需要椭球高，因此：

```text
INSPVA ellipsoidal height = INSPVAX MSL height + undulation
```

## 7.5 其他字段

以下字段直接传递：

```text
week / seconds
latitude / longitude
north / east / up velocity
roll / pitch / azimuth
INS status
```

NovAtel ASCII header 中可获得的 port、sequence、idle time、time status、receiver status 等也尽量保持原值；缺失时使用合理默认值。

## 7.6 `--only-inspva`

默认情况下：

```bat
pos_convert.exe --inspvax-to-inspva input.log output.log
```

行为为：

- 合法 INSPVAX → 转换成 INSPVA；
- 非 INSPVAX 记录 → 原样复制；
- malformed INSPVAX → 原样复制，同时输出诊断；
- 因 `--rate` 被降频丢弃的合法 INSPVAX → 不复制。

因此该模式可用于混合日志中“就地把 INSPVAX 替换为 INSPVA”。

如果只希望输出干净的 INSPVA：

```bat
--only-inspva
```

例如：

```bat
pos_convert.exe --inspvax-to-inspva input.log output.inspva --only-inspva
```

这时非 INSPVAX 和 malformed INSPVAX 都不会写入输出。

## 7.7 malformed 诊断

程序会识别例如：

```text
缺少 ;
缺少 *
header 字段不足
body 字段不足
消息名不是 INSPVAXA
GNSS week 非法
GNSS second 非法
必需字段无法转换为数字
经纬度/时间越界
```

为避免错误日志过多，只详细打印前 10 条 malformed INSPVAX 的预览和原因。

如果存在 malformed INSPVAX，程序可以完成其余合法记录的转换，但最终返回码为 `3`，便于批处理脚本发现输入中存在格式问题。

## 7.8 示例

混合日志替换：

```bat
pos_convert.exe --inspvax-to-inspva input.log output.log
```

仅输出 INSPVA：

```bat
pos_convert.exe --inspvax-to-inspva input.log output.inspva --only-inspva
```

仅输出 INSPVA，并自动降为 10 Hz：

```bat
pos_convert.exe --inspvax-to-inspva input.log output.inspva --only-inspva --rate 10
```

手动指定 100 Hz → 10 Hz：

```bat
pos_convert.exe --inspvax-to-inspva input.log output.inspva --only-inspva --rate 100 10
```

---

# 8. 模式 4：INSPVAX → BESTPOS

## 8.1 命令格式

```bat
pos_convert.exe --inspvax-to-bestpos input.log output.bestpos [options]
```

## 8.2 为什么该转换比 INSPVA → BESTPOS 信息更完整

INSPVAX 本身已经包含 BESTPOS 所需的很多位置质量信息，例如：

```text
INS status
position type
latitude
longitude
MSL height
undulation
latitude sigma
longitude sigma
height sigma
extended solution status
```

所以这些字段可以直接映射，不需要人工猜测。

## 8.3 字段映射

| INSPVAX | BESTPOS |
|---|---|
| INS status | solution status |
| position type | position type |
| latitude | latitude |
| longitude | longitude |
| MSL height | height |
| undulation | undulation |
| latitude sigma | latitude sigma |
| longitude sigma | longitude sigma |
| height sigma | height sigma |
| extended solution status | extended solution status 低字节 |

特别注意：

```text
INSPVAX Height = MSL height
BESTPOS Height = MSL height
```

因此该模式 **不进行高度换算**。

## 8.4 卫星数

INSPVAX 不提供 BESTPOS 所需的卫星计数字段，所以默认写 0。

如果已知希望写入的卫星数：

```bat
--sat-num 30
```

当前同一个值会写入 BESTPOS 的 tracked / solution / L1 / multi-frequency 等计数字段，仅用于格式兼容；它并不是从 INSPVAX 实际恢复出的卫星使用情况。

## 8.5 状态映射

当：

```text
INS status = INS_SOLUTION_GOOD
```

输出：

```text
SOL_COMPUTED,<INSPVAX 原始 position type>
```

否则：

```text
INSUFFICIENT_OBS,NONE
```

## 8.6 CRC

和 INSPVAX→INSPVA 一样：

```text
输入 CRC：忽略
输出 BESTPOS CRC：重新计算
```

## 8.7 示例

基本转换：

```bat
pos_convert.exe --inspvax-to-bestpos input.log output.bestpos
```

写卫星数：

```bat
pos_convert.exe --inspvax-to-bestpos input.log output.bestpos --sat-num 30
```

自动降为 10 Hz：

```bat
pos_convert.exe --inspvax-to-bestpos input.log output.bestpos --rate 10
```

组合使用：

```bat
pos_convert.exe --inspvax-to-bestpos input.log output.bestpos --sat-num 30 --rate 10
```

---

# 9. 降频功能

四种转换模式均支持降频。

只支持 **整数倍降频**，不做插值、不做重采样拟合。

## 9.1 自动识别源频率

例如源文件实际为 100 Hz，想输出 10 Hz：

```bat
--rate 10
```

完整命令示例：

```bat
pos_convert.exe --inspvax-to-bestpos input.log output.bestpos --rate 10
```

程序先自动识别源频率，再检查：

```text
inputHz % outputHz == 0
```

若自动识别为：

```text
100 Hz
```

则：

```text
decimationFactor = 100 / 10 = 10
```

保留：

```text
第 1、11、21、31、... 条有效目标记录
```

即按照“有效记录序号”抽取，而不是对时间轴进行插值。

## 9.2 手动指定源频率

也可以绕过自动检测：

```bat
--rate 100 10
```

表示：

```text
输入频率  = 100 Hz
输出频率  = 10 Hz
抽取因子  = 10
```

## 9.3 只支持整除

支持：

```text
100 -> 10
100 -> 20
100 -> 25
50  -> 10
50  -> 5
20  -> 5
10  -> 1
```

不支持：

```text
100 -> 30
50  -> 12
10  -> 3
```

不可整除时直接报错，不会近似输出。

## 9.4 自动频率检测实现

为了避免只根据第一、第二个时间戳误判，程序会在文件前部收集一批有效时间间隔：

```text
最多 4096 条有效记录
最多 1024 个有效时间间隔
```

主要流程：

1. 读取有效记录的 GNSS/UTC 时间；
2. 忽略重复时间和时间回退；
3. 处理 GNSS week rollover；
4. 忽略大于 10 s 的巨大间隔；
5. 将时间间隔排序；
6. 使用约第 20 百分位的时间间隔作为代表间隔，而不是直接使用最小值；
7. 计算 `frequency = 1 / interval`；
8. 频率必须接近整数 Hz；
9. 代表间隔相对于整数频率名义周期允许约 2% 误差；
10. 再检查所有间隔是否可以解释为名义周期的整数倍。

可靠性要求包括：

```text
至少 5 个有效时间间隔
至少 80% 的间隔符合检测出的时间网格
至少 20% 的间隔是单周期间隔
检测频率范围 1 ~ 10000 Hz
```

对单个间隔进行网格一致性检查时，允许约一个名义周期的 5% 误差，并允许丢历元导致的 2×、3×... 周期间隔。

如果时间戳过于混乱，程序会拒绝自动检测，并提示改用手动：

```bat
--rate IN_HZ OUT_HZ
```

## 9.5 重要说明：降频不是插值

例如：

```text
源频率 100 Hz
目标频率 10 Hz
```

程序不是生成新的 0.1 s 网格并寻找最近时间，也不会插值，而是：

```text
每 10 条有效记录保留 1 条
```

因此如果源文件存在大量非规则丢历元，建议先检查数据连续性；自动检测会尽量拒绝过于不规则的文件，但通过检测后实际抽取仍以有效记录计数为准。

---

# 10. 校验码实现

## 10.1 NMEA GGA

GGA checksum 为 `$` 和 `*` 之间所有字符逐字节 XOR：

```text
$GNGGA,...*XX
```

`$`、`*`、checksum 字符本身以及 CR/LF 不参与 XOR。

## 10.2 NovAtel ASCII CRC32

INSPVA/BESTPOS 使用 NovAtel ASCII 32-bit CRC。

程序使用多项式：

```text
0xEDB88320
```

计算区域为：

```text
# 后第一个字符
一直到
* 前最后一个字符
```

不包含：

```text
#
*
原 CRC 字符串
\r\n
```

每一条新生成的 INSPVA/BESTPOS 都重新计算 CRC，因此不会复用输入日志中可能错误的 CRC。

---

# 11. 文件读取与大文件处理

程序整体采用流式处理，适合较大的日志文件。

## 11.1 Position / INSPVA 模式

使用：

```cpp
fgets()
```

逐行读取，固定行缓冲区为 4096 字节。

不会把整个文件加载到内存。

## 11.2 INSPVAX 模式

考虑到 INSPVAX 行可能较长，使用动态 record reader：

- 初始缓冲区 4096 字节；
- 行过长时动态扩容；
- 不依赖固定 4096 字节 INSPVAX 行长；
- 仍然是单条记录流式处理。

这也延续了之前 INSPVAX→INSPVA v3 对较长日志语句的处理思路。

## 11.3 自动检测频率时

自动频率检测只扫描文件前部有限数量的有效记录，然后：

```text
rewind(file)
```

重新从文件开头执行正式转换，不会把扫描结果缓存成整个文件副本。

---

# 12. 参数汇总

## 12.1 通用参数

| 参数 | 含义 |
|---|---|
| `-h` | 显示帮助 |
| `--help` | 显示帮助 |
| `--rate OUT_HZ` | 自动识别输入频率并降频 |
| `--rate IN_HZ OUT_HZ` | 手动指定输入/输出频率 |
| `--sat-num N` | 写入 GGA/BESTPOS 的固定卫星数；INSPVAX→INSPVA 不使用 |

## 12.2 模式 1 专用

| 参数 | 默认值 | 含义 |
|---|---:|---|
| `--week N` | 0 | 输入无 Week 时指定 GNSS/UTC week |
| `--utc-offset SEC` | 18 | GNSS-UTC 时间差 |
| `--geoid-sep M` | 0 | geoid undulation |
| `--hdop VALUE` | 1.0 | GGA HDOP |

## 12.3 INSPVA → BESTPOS 专用

| 参数 | 默认值 | 含义 |
|---|---:|---|
| `--geoid-sep M` | 0 | 用于椭球高→MSL 高 |
| `--pos-type TYPE` | `INS_PSRSP` | 有效 INS 解对应的 BESTPOS position type |
| `--horiz-sigma M` | 0 | 纬度/经度标准差 |
| `--height-sigma M` | 0 | 高度标准差 |
| `--sat-num N` | 0 | 固定卫星数 |

## 12.4 INSPVAX → INSPVA 专用

| 参数 | 含义 |
|---|---|
| `--only-inspva` | 只输出转换后的 INSPVA，不复制其他记录 |
| `--ignore-input-crc` | 兼容旧命令；当前始终忽略输入 INSPVAX CRC |

## 12.5 INSPVAX → BESTPOS

主要使用：

```text
--sat-num N
--rate ...
```

`--geoid-sep` / `--pos-type` / sigma 参数在这个模式不需要，因为 INSPVAX 已包含对应信息。

---

# 13. 常见使用示例

## 13.1 查看帮助

```bat
pos_convert.exe -h
```

```bat
pos_convert.exe --help
```

## 13.2 Position → 三种输出

```bat
pos_convert.exe input.txt result --week 2362
```

```bat
pos_convert.exe input.txt result --week 2362 --rate 10
```

```bat
pos_convert.exe input.txt result --week 2362 --geoid-sep -17.0 --sat-num 30 --hdop 0.8
```

```bat
pos_convert.exe input.txt result --week 2362 --rate 100 10
```

## 13.3 INSPVA → BESTPOS

```bat
pos_convert.exe --inspva-to-bestpos input.inspva output.bestpos
```

```bat
pos_convert.exe --inspva-to-bestpos input.inspva output.bestpos --geoid-sep -17.0
```

```bat
pos_convert.exe --inspva-to-bestpos input.inspva output.bestpos --pos-type INS_RTKFIXED
```

```bat
pos_convert.exe --inspva-to-bestpos input.inspva output.bestpos --horiz-sigma 0.02 --height-sigma 0.04 --sat-num 30
```

```bat
pos_convert.exe --inspva-to-bestpos input.inspva output.bestpos --rate 10
```

## 13.4 INSPVAX → INSPVA

```bat
pos_convert.exe --inspvax-to-inspva input.log output.log
```

```bat
pos_convert.exe --inspvax-to-inspva input.log output.inspva --only-inspva
```

```bat
pos_convert.exe --inspvax-to-inspva input.log output.inspva --only-inspva --rate 10
```

```bat
pos_convert.exe --inspvax-to-inspva input.log output.inspva --only-inspva --rate 100 10
```

## 13.5 INSPVAX → BESTPOS

```bat
pos_convert.exe --inspvax-to-bestpos input.log output.bestpos
```

```bat
pos_convert.exe --inspvax-to-bestpos input.log output.bestpos --sat-num 30
```

```bat
pos_convert.exe --inspvax-to-bestpos input.log output.bestpos --rate 10
```

```bat
pos_convert.exe --inspvax-to-bestpos input.log output.bestpos --sat-num 30 --rate 10
```

---

# 14. 程序内部实现结构

源码主要可以理解为下面几个模块。

## 14.1 命令行解析

核心数据结构：

```cpp
ProgramOptions
```

负责：

- 判断 4 种模式；
- 解析 `--week` / `--rate` / `--geoid-sep` 等参数；
- 检查某个参数是否允许用于当前模式；
- 检查频率是否为正整数以及能否整除。

## 14.2 输入解析

主要结构：

```cpp
PositionRecord
InspvaRecord
InspvaxRecord
```

分别保存三种输入记录中转换所需的数据。

解析函数会严格检查数值字符串，避免类似：

```text
8:50:00.00
```

被普通 `%lf` 误解析成数字 `8`。

这也是 GGA 时间错误问题在当前版本中的修复方式之一。

## 14.3 时间处理

包括：

- UTC HMS → GGA 时间；
- UTC week/seconds + UTC offset → GNSS week/seconds；
- 跨周 rollover；
- INSPVA/INSPVAX GNSS 时间差计算；
- 自动频率检测。

## 14.4 坐标/速度处理

包括：

- 十进制度 → NMEA ddmm.mmmm；
- ECEF 速度 → NEU；
- Heading → `[0,360)` azimuth；
- 椭球高 ↔ MSL height 的必要转换。

## 14.5 消息生成

主要构造函数对应：

```text
BuildGgaSentence
BuildInspvaSentence
BuildBestPosSentence
BuildBestPosFromInspva
BuildInspvaFromInspvax
BuildBestPosFromInspvax
```

每个函数负责把内部 record 转成完整 ASCII 语句。

## 14.6 校验码

分为：

```text
NMEA XOR checksum
NovAtel CRC32
```

所有生成日志在格式化 body 后立即重新计算校验码。

## 14.7 主处理循环

对应：

```text
RunPositionToAll
RunInspvaToBestPos
RunInspvaxToInspva
RunInspvaxToBestPos
```

处理流程基本都是：

```text
打开输入
→ 可选自动检测频率
→ 打开输出
→ 逐条读取
→ 解析目标类型
→ 按 decimationFactor 抽取
→ 字段转换
→ 重新计算 CRC/checksum
→ 写文件
→ 打印统计
```

---

# 15. 返回码

当前主要返回码：

```text
0  正常成功；帮助命令也返回 0
1  参数、文件打开、频率检测、写文件等致命错误
3  INSPVAX 模式转换完成，但发现 malformed INSPVAX
```

因此在 BAT 或自动化脚本中可通过：

```bat
if errorlevel 1 ...
```

判断是否存在问题。

需要注意 `3` 表示可能已经生成了部分有效输出，但源文件中存在 malformed INSPVAX，需要检查控制台诊断。

---

# 16. 已知限制和设计假设

1. **Position 模式的 Q 信息有限。** `Q>0` 只能被简化映射为有效解，无法自动判断 SINGLE / PSRDIFF / RTK FLOAT / RTK FIXED。
2. **Position 模式的 GGA 卫星数和 HDOP 不是从源记录恢复。** 需要使用 `--sat-num`、`--hdop` 给固定值。
3. **INSPVA→BESTPOS 无法恢复全部 BESTPOS 字段。** position type、geoid、sigma、卫星数需要人工补充。
4. **INSPVAX→BESTPOS 的卫星数无法从 INSPVAX 得到。** `--sat-num` 只是格式兼容值。
5. **自动降频只支持整数 Hz 和整数倍。** 不支持 30 Hz 从 100 Hz 这类非整除情况，也不支持插值。
6. **实际抽取按有效记录计数。** 即使自动检测使用时间戳判断频率，正式降频仍是每 N 条有效记录取 1 条。
7. **INSPVA 普通读取缓冲区为 4096 字节。** 标准 INSPVA 通常远低于此长度；INSPVAX 使用动态行缓冲，不受该固定长度限制。
8. **INSPVAX 输入 CRC 值不校验。** 这是刻意设计，用于处理源日志 CRC 错误但字段本身可用的情况。
9. **INSPVAX 解析目前针对 `#INSPVAXA`。** 非该消息不会被当作有效 INSPVAX。
10. **输出文件会以写模式创建。** 如果同名输出已存在，会被覆盖/截断，使用前应确认文件名。

---

# 17. 建议的使用流程

第一次处理一种新数据时，建议：

```text
1. pos_convert.exe -h
2. 先截取一小段数据测试
3. 不加 --rate，确认字段/时间/高度正确
4. 检查输出前几条 GGA / INSPVA / BESTPOS
5. 再开启 --rate 进行完整文件转换
6. 查看程序末尾统计，确认有效记录数、输出记录数、Rate-dropped 和 malformed 数量
```

对于 INSPVAX，建议重点关注：

```text
Malformed INSPVAX
Input CRC : ignored
Output CRC: recalculated
```

对于自动降频，建议重点关注：

```text
Rate conversion
Rate detection
nominal interval
consistent intervals
```

---

# 18. 格式参考

程序输出格式主要参考 NovAtel OEM7 / NMEA 规范：

- NovAtel INSPVA：`https://docs.novatel.com/OEM7/Content/SPAN_Logs/INSPVA.htm`
- NovAtel INSPVAX：`https://docs.novatel.com/OEM7/Content/SPAN_Logs/INSPVAX.htm`
- NovAtel BESTPOS：`https://docs.novatel.com/OEM7/Content/Logs/BESTPOS.htm`

如果后续要继续扩展，同一框架可以继续增加例如 `BESTVEL`、`INSPOS` 或其他 NovAtel ASCII 日志转换模式。
