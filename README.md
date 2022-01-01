# Multi-Top
Turn any browser capable device into a monitor

This project was inspired in part by my good friend, [Miguel](https://github.com/miguelcmaramara) who uses 3 laptops all controlled by one keyboard and mouse using [Barrier](https://github.com/debauchee/barrier). This setup allows him to have much more hardware resources compared to a single computer. However, as these machines all have different IP addresses and file systems, losing out on the synchronization of actual external monitors connected to a single device could be a hindrance to productivity.

This project is designed to replicate the experience of an external monitor on another device as close as possible.

## OS Support
- Windows (with custom virutal monitor driver)
- Linux (with `xrandr`)

## Dependencies
- [pion/webrtc](https://github.com/pion/webrtc)
- [gorilla/websocket](https://github.com/gorilla/websocket)
