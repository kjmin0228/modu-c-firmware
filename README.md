> [!NOTE]
> 키맵 편집 및 다운로드: https://github.com/kjmin0228/modu-c-zmk-config (원본: https://github.com/22sh22/modu-c-zmk-config)

# MODU Firmware

MODU-C 키보드용 ZMK 펌웨어 소스입니다.

빌드하려면 `west build`가 동작하는 ZMK 개발 환경과 Zephyr SDK가 필요합니다.

Windows에서는 다음 명령으로 좌·우 BLE 펌웨어를 생성할 수 있습니다.

```bat
build.bat C:\zmk\app
```

생성된 `modu_left.uf2`와 `modu_right.uf2`는 `outputs` 폴더에 저장됩니다.

**키매핑 파일의 위치는 `modu-module/boards/shields/modu/modu.keymap` 입니다.**

미리 빌드된 펌웨어는 GitHub Releases에서 배포합니다.

## 이 포크에서 바뀐 점

[22sh22/modu-c-firmware](https://github.com/22sh22/modu-c-firmware)를 fork 해서 배터리를 아끼도록 손본 개인용 포크입니다. [kjmin0228/modu-c-zmk-config](https://github.com/kjmin0228/modu-c-zmk-config)가 이 펌웨어로 빌드합니다.

- **딥슬립**: 30분 동안 입력이 없으면 양쪽 모두 딥슬립에 들어가고, 키를 누르면 깨어납니다.
- **대기 전력 절감**: 키 입력 감지를 폴링에서 인터럽트 방식으로 바꿔 쉬는 동안 전력을 덜 씁니다.
- **LED 절전**: 상태 LED 밝기를 낮추고(사용 중 40%, 대기 중 5%), 딥슬립에서는 끕니다. 색의 의미(페어링 대기, 연결됨, 끊김)는 그대로입니다.
- **UF2 바로 생성**: 빌드하면 변환 없이 바로 쓸 수 있는 UF2 파일이 나옵니다.

## License

Copyright (c) 2026 EKS Inc. · Created by Ryu

MODU 고유 소스는 비상업적 사용·수정·동일 조건 재배포만 허용됩니다.
자세한 조건은 [LICENSE](LICENSE), 외부 코드 고지는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)를 참고하세요.
