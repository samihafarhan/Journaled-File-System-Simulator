# Journaled File System Simulator

This project implements a journaling mechanism for a simulated file system. Its primary purpose is to maintain file system integrity by ensuring that metadata and data updates are recorded in a journal before they are applied to the main storage area. This approach protects the system from data corruption caused by unexpected shutdowns or crashes.

---

### Core Components

The system is organized into three primary modules that handle the lifecycle of a file system transaction:

1. File System Structure and Management
This module defines the fundamental layout of the file system, including the Superblock, Inodes, and the disk block structure. It handles the initial setup and basic read/write operations for the virtual disk image.

2. Transaction Management
This module manages the writing of data records to the journal. It handles the process of grouping multiple block changes into a single transaction and appending a commit record once the logging is complete.

3. Recovery and Installation
This module provides the logic to scan the journal during system startup. It identifies committed transactions that were not yet written to the main disk and replays them to ensure the file system reaches a consistent state.

---

### Technical Features

* Transaction Logging: Captures block-level changes and stores them as data records within the journal.
* Atomic Commits: Uses specific commit markers to distinguish between completed transactions and those interrupted by a crash.
* Crash Recovery: Implements a recovery loop that scans the journal and updates the main file system based on committed log entries.
* Virtual Disk Interaction: Operates on a simulated disk image, allowing for the testing of file system operations without physical hardware risks.

---

### Specifications

* Programming Language: C
* Block Size: 4096 bytes
* Journal Size: 16 dedicated blocks
* Supported Metadata: Superblock and Inode management

---

### Author

Samiha Farhan
Final-year Computer Science Undergraduate
