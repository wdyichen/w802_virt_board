# W802 Virtual Board Project

## 功能概述

1. UART0
   - TX - PB19
   - RX - PB20

2. TFT-LCD (NV3041A)
   - LED - PA2
   - RST  - PA7
   - CLK - PA9
   - MOSI - PA10
   - DCX  - PA12
   - CS  - PA14

3. I2C (SHT30)
   - SCL  - PA1
   - SDA - PA4

4. KEY (GPIO)
   - SW1 - PA0
   - SW2 - PB16
   - SW3 - PB18
   - SW4 - PB21
   - SW5 - PB22

5. RGB-LED
   - RED - PB24
   - GREEN - PB25
   - BLUE - PB26

6. NTC (MF52-103)
   - ADC2 - PA3

7. PSRAM (8M)
   - CK - PB0
   - CS - PB1
   - D0 - PB2
   - D1 - PB3
   - D2 - PB4
   - D3 - PB5

8. SDMMC
   - CLK - PB6
   - CMD - PB7
   - DAT0 - PB8
   - DAT1 - PB9
   - DAT2 - PB10
   - DAT3 - PB11

9. PWM (optional)
   - PWM2 - PB24
   - PWM3 - PB25
   - PWM4 - PB26

10. BEEP
   - PB29

11. I2S (unrealized)
    - BCLK - PB12
    - LRCLK - PB13
    - DO - PB14
    - DI -  PB15(not used)
    - MCLK - PB17


## 环境要求

   使用 W80X 模拟器制作的 [W802 虚拟开发板](https://yichen.link/?log=blog&id=97)，用法：

`w80x -elf .\build\w802_virt_board.elf -img .\build\w802_virt_board.fls` 或 `w80x -prj`

使用 SD 卡时，

`w80x -elf .\build\w802_virt_board.elf -img .\build\w802_virt_board.fls -sd .\sd.img`

SD 卡镜像可使用 qemu-img 工具制作： `qemu-img.exe create -f raw sd.img 1G`。
