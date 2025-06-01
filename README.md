# Process and Thread Management System in C

!*A comprehensive system for managing processes, threads, and IPC mechanisms in Linux*

## 📖 Overview
This project implements an advanced Process and Thread Management System using C on Kali Linux. It demonstrates core operating system concepts including process management, threading, IPC, and synchronisation.

## ✨ Features
- **Process Management** (fork, wait, kill)
- **Thread Management** (pthreads with mutexes/condition variables)
- **IPC Mechanisms** (pipes, shared memory, sockets)
- **Synchronisation** (semaphores, mutexes)
- **Logging System** with timestamped operations

## 📂 Files
project/
├── main.c # Main source code
├── docs/
  ├── Project_Report.docx # Complete documentation
  └── Presentation.pptx # Project slides

📚 Documentation
Report: docs/102_Processs_and_Thread_Management.docx
Presentation: docs/102_Processs_and_Thread_Management.pptx

💻 Technical Stack
Language: C (POSIX compliant)
Platform: Kali Linux

Key Libraries:
<pthread.h> for threading
<sys/ipc.h> for IPC
<semaphore.h> for synchronization

👥 Contributors
Zunaira Shafqat

## 🚀 Quick Start

### Compile & Run
```bash
# Clone the repository
https://github.com/ZunairaShafqat/ProcessAndThreadManagement.git
cd project

# Compile (requires gcc)
gcc main.c -o main -lpthread

# Run
./main
