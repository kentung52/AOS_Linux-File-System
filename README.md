# AOS_Linux-File-System
Advanced Operating Systems -System Protection &amp; Distributed Synchronization

Objective:
To implement a client-server system that manages file access permissions and ensures proper synchronization during concurrent operations.

Key Features:
  1. File Management:
          Follow UNIX file system permissions (read, write, execute) for file owners, group  members,and others.
          Support dynamic creation, reading, writing, and modifying file permissions.
          Utilize capability lists to manage file permissions and track changes after each operation.
     
  2. Client Groups:
        Two client groups: AOS-students and CSE-students, each with at least three clients.
        Permissions vary by file owner, group members, and others.
     
  3. Supported Commands:
        create <filename> <permissions>: Create a file with specified permissions (e.g., rwr---).
        read <filename>: Download a file from the server if the client has the required permission.
        write <filename> o/a: Upload data to an existing file (overwrite or append based on the parameter o or a).
        mode <filename> <permissions>: Modify a file's permissions dynamically.
     
  4.Concurrency Rules:
        A file being written cannot be read or written by other clients simultaneously.
        A file being read cannot be written by other clients but can be read concurrently by multiple clients.
        Ensure synchronization to enforce these rules in a multi-client environment.
        
  5. Demonstration Requirements:
        Showcase the changes in capability lists for each file operation.
        Ensure the server connects multiple clients and supports concurrent read/write operations on large files.


     
