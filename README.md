# UDP File Transfer Demo (C)

This project demonstrates a simple **UDP-based file transfer system** written in C, featuring a sender and receiver program. It allows you to send a message and a file from one process to another using the **UDP socket** interface. A `run.sh` script automates the entire process using `tmux` so you can observe both programs in action.

---

## 📦 Features

- Transfer files over UDP from sender to receiver
- Predefined test file menu
- Option to send your own file
- Automated setup with `tmux`
- Clean testing environment with `clean.sh`

---

## 🧰 Prerequisites

Before running, make sure you have:

- GCC (`gcc`)
- `tmux` installed

You can install `tmux` using:
For DebIan like systems
```bash
sudo apt update
sudo apt install tmux
```

For Arch
```bash
sudo pacman -Syu tmux
```
## Future work
- It doesn't check anything
- Big files arrive broken, needs a fix
- And no ACK, because it is a simple implementation.

