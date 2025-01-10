#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>

#define PORT 12500
#define BUFFER_SIZE (512 * 1024)
#define MAX_FILES 100
#define MAX_USERS 20

typedef struct {
    char username[20];
    char group[20];
} User;

typedef struct {
    char filename[50];
    char permissions[7]; // rw-r--r
    char owner[20];
    char group[20];
    char content[BUFFER_SIZE];
    int size;
    char created_at[30];
    pthread_rwlock_t lock; // read-write lock
    pthread_mutex_t file_mutex; // mutex for file
    pthread_cond_t file_cond; // condition variable used for synchronous waiting
    int readers;    // number of current reading clients
    int is_writing; // indicates if it's currently being written to (0: no, 1: yes)
} File;

File files[MAX_FILES];
User users[MAX_USERS];
int file_count = 0;
int user_count = 0;
pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function prototypes
void *handle_client(void *arg);
int find_file(const char *filename);
void add_user(const char *username, const char *group);
int check_permission(const char *username, const File file, char op);
const char* get_user_group(const char *username);
void send_user_list(int client_socket);
void show_capability_list();
void cleanup_files();
void initialize_large_file(); 

int main() {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket 創建失敗");
        exit(EXIT_FAILURE);
    }

    // Bind socket
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt 失敗");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind 失敗");
        exit(EXIT_FAILURE);
    }
    
    initialize_large_file();

    // Listen for clients
    if (listen(server_fd, 3) < 0) {
        perror("Listen 失敗");
        exit(EXIT_FAILURE);
    }
    printf("服務器啟動，監聽端口 %d\n", PORT);

    // Accept clients
    while (1) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            perror("接受客戶端失敗");
            exit(EXIT_FAILURE);
        }
        pthread_t thread_id;
        int *pclient = malloc(sizeof(int));
        if (pclient == NULL) {
            perror("內存分配失敗");
            close(client_socket);
            continue;
        }
        *pclient = client_socket;
        pthread_create(&thread_id, NULL, handle_client, pclient);
        pthread_detach(thread_id); // Detach thread to free resources
    }

    atexit(cleanup_files);

    return 0;
}

void *handle_client(void *arg) {
    int client_socket = *((int *)arg);
    free(arg);
    char buffer[BUFFER_SIZE];
    char local_current_user[20] = ""; // The current user for each client
    char current_user[20] = "";
    int expected = 0;

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int read_size = read(client_socket, buffer, sizeof(buffer));
        if (read_size <= 0) {
            // Client disconnected
            printf("客戶端斷開連接。\n");
            break;
        }

        // Define command parameters
        char command[20], arg1[50], arg2[2], arg3[BUFFER_SIZE];
        memset(command, 0, sizeof(command));
        memset(arg1, 0, sizeof(arg1));
        memset(arg2, 0, sizeof(arg2));
        memset(arg3, 0, sizeof(arg3));
        sscanf(buffer, "%s %s %s %[^\n]", command, arg1, arg2, arg3);
    
        //pthread_mutex_lock(&data_mutex);

        // Handle commands
        if (strcmp(command, "create_user") == 0) {
            // Create user
            if (user_count < MAX_USERS) {
                // Check if user already exists
                int exists = 0;
                for (int i = 0; i < user_count; i++) {
                    if (strcmp(users[i].username, arg1) == 0) {                    
                        exists = 1;
                        //pthread_mutex_unlock(&data_mutex);
                        break;
                    }
                }
                if (exists) {
                    snprintf(buffer, sizeof(buffer), "用戶 %s 已存在。\n", arg1);
                    //pthread_mutex_unlock(&data_mutex);
                } else {
                    add_user(arg1, arg2); // Create user based on username and group
                    snprintf(buffer, sizeof(buffer), "User %s added to group %s.\n", arg1, arg2);
                    //pthread_mutex_unlock(&data_mutex);
                }
               // pthread_mutex_unlock(&data_mutex);
            } else {
                snprintf(buffer, sizeof(buffer), "用戶數量已達上限。\n");
                //pthread_mutex_unlock(&data_mutex);
            }
            //pthread_mutex_unlock(&data_mutex);
        } else if (strcmp(command, "list_users") == 0) {
            // List all users
            send_user_list(client_socket);
            //pthread_mutex_unlock(&data_mutex);
            continue; // Response already sent, skip subsequent code
        } else if (strcmp(command, "set_user") == 0) {
            // Check if user exists
            int exists = 0;
            for (int i = 0; i < user_count; i++) {
                if (strcmp(users[i].username, arg1) == 0) {
                    exists = 1;
                    strncpy(local_current_user, users[i].username, sizeof(local_current_user) - 1);
                    local_current_user[sizeof(local_current_user)-1] = '\0';
                    strncpy(current_user, local_current_user, sizeof(current_user) - 1); 
                    break;
                }               
            }
            if (exists) {
                
                snprintf(buffer, sizeof(buffer), "User: %s (%s)\nAvailable commands:\n1. create <filename> <permissions>\n2. read <filename>\n3. write <filename> o/a\n4. mode <filename> <permissions>\n5. exit\n", 
                         local_current_user, get_user_group(local_current_user));  
                show_capability_list();                  
            } else {
                snprintf(buffer, sizeof(buffer), "用戶 %s 不存在。\n", arg1);
            }
            
        } else if (strcmp(command, "create") == 0) {
        
            if (strlen(local_current_user) == 0) {
                snprintf(buffer, sizeof(buffer), "未設定用戶。請先登入。\n");
            } else if (find_file(arg1) != -1) {
                snprintf(buffer, sizeof(buffer), "檔案 '%s' 已存在。\n", arg1);
            }  else if (file_count < MAX_FILES) {
                pthread_mutex_init(&files[file_count].file_mutex, NULL);
        
                // Create file
                strncpy(files[file_count].filename, arg1, sizeof(files[file_count].filename) - 1);
                files[file_count].filename[sizeof(files[file_count].filename) - 1] = '\0';            
           
                strncpy(files[file_count].permissions, arg2, sizeof(files[file_count].permissions) - 1);
                files[file_count].permissions[sizeof(files[file_count].permissions) - 1] = '\0';           

                // Set owner
                strncpy(files[file_count].owner, current_user, sizeof(files[file_count].owner) - 1);
                files[file_count].owner[sizeof(files[file_count].owner) - 1] = '\0';
        
                // Initialize read-write lock
                pthread_rwlock_init(&files[file_count].lock, NULL);        

                // Ensure the group is valid
                const char *user_group = get_user_group(current_user);
        
                // Set group
                strncpy(files[file_count].group, user_group, sizeof(files[file_count].group) - 1);
                files[file_count].group[sizeof(files[file_count].group) - 1] = '\0';

                files[file_count].size = 0;
                files[file_count].content[0] = '\0';  // Initialize content 

                // Record creation time
                time_t now = time(NULL);
                strftime(files[file_count].created_at, sizeof(files[file_count].created_at), "%Y-%m-%d %H:%M:%S", localtime(&now));
                file_count++;

                // Display created file details
                snprintf(buffer, sizeof(buffer), "File '%s' Created，Permissions %s，Owner：%s，Group：%s。\n", 
                         arg1, arg2, files[file_count-1].owner, files[file_count-1].group);  
                show_capability_list(); // Show capability list
            } else {
                snprintf(buffer, sizeof(buffer), "The number of files has reached the upper limit.\n");
            }
         
        } else if (strcmp(command, "read") == 0) {
            if (strlen(current_user) == 0) {
                snprintf(buffer, sizeof(buffer), "No user has been configured. Please log in first.\n<END_OF_FILE>");
            }
            else {
                // File read
                int index = find_file(arg1);
                if (index == -1) {
                    snprintf(buffer, sizeof(buffer), "File not found.\n<END_OF_FILE>");
                } else if (!check_permission(current_user, files[index], 'r')) {
                    snprintf(buffer, sizeof(buffer), "Permissions denied\n<END_OF_FILE>");
                } else {
                    pthread_mutex_lock(&files[index].file_mutex);                    
                    if (files[index].is_writing){         
                        snprintf(buffer, sizeof(buffer), "檔案 '%s' 正在被其他使用者寫入，無法讀取。\n<END_OF_FILE>", arg1);
                        pthread_mutex_unlock(&files[index].file_mutex);                     
                    } else{
                        
                        files[index].readers++;
                        pthread_mutex_unlock(&files[index].file_mutex);

                        // Acquire read lock and perform reading
                        pthread_rwlock_rdlock(&files[index].lock);
                        printf("reading...\n");
                        sleep(2); // Simulate read delay
                        int content_len = strlen(files[index].content);
                        int sent = 0;
                        // Send file content in segments
                        while (sent < content_len) {
                            int chunk_size = content_len - sent;
                            if (chunk_size > (int)(sizeof(buffer) - 1)) {
                                chunk_size = (int)(sizeof(buffer) - 1);
                            }
                            memcpy(buffer, files[index].content + sent, chunk_size);
                            buffer[chunk_size] = '\0';
                            write(client_socket, buffer, chunk_size);
                            sent += chunk_size;
                        }
                
                        // Send end marker
                        const char *end_marker = "\n<END_OF_FILE>\n";
                        write(client_socket, end_marker, strlen(end_marker));
                        pthread_rwlock_unlock(&files[index].lock);

                        pthread_mutex_lock(&files[index].file_mutex);
                        files[index].readers--;
                        pthread_mutex_unlock(&files[index].file_mutex);
                    }                
                }
            }
                 
        }
        
        else if (strcmp(command, "write") == 0) {        
            if (strlen(current_user) == 0) {
                snprintf(buffer, sizeof(buffer), "No user has been configured. Please log in first.\n");
            } else {
                int index = find_file(arg1);                
                if (index == -1) {
                    snprintf(buffer, sizeof(buffer), "File not found.\n");
                } else if (!check_permission(current_user, files[index], 'w')) {
                    snprintf(buffer, sizeof(buffer), "Permissions denied.\n");
                } else {           
                    pthread_mutex_lock(&files[index].file_mutex);
                    if (files[index].is_writing || files[index].readers > 0) {                
                        snprintf(buffer, sizeof(buffer), "檔案 '%s' 正在被其他使用者操作，無法寫入。\n", arg1);
                        pthread_mutex_unlock(&files[index].file_mutex);               
                    }else{                            
                        // Mark as being written
                        files[index].is_writing = 1;
                        pthread_mutex_unlock(&files[index].file_mutex);

                        // Notify the client to send content
                        snprintf(buffer, sizeof(buffer), "Ready to write to file '%s'. Send content.\n", arg1);
                        write(client_socket, buffer, strlen(buffer));

                        // Wait for the client to send content
                        memset(buffer, 0, sizeof(buffer));
                        int read_size = read(client_socket, buffer, sizeof(buffer));
                        if (read_size <= 0) {
                            // If the client disconnects, reset writing status
                            pthread_mutex_lock(&files[index].file_mutex);
                            files[index].is_writing = 0;
                            pthread_mutex_unlock(&files[index].file_mutex);
                            printf("Client disconnected before sending content.\n");
                            return NULL;
                        }

                        // Acquire write lock and perform writing
                        pthread_rwlock_wrlock(&files[index].lock);

                        printf("Writing to file '%s'...\n", arg1);
                        sleep(3); // Simulate write delay
                        if (strcmp(arg2, "o") == 0) {
                            strncpy(files[index].content, buffer, sizeof(files[index].content) - 1);
                            files[index].content[sizeof(files[index].content) - 1] = '\0';
                            files[index].size = strlen(files[index].content);
                        } else if (strcmp(arg2, "a") == 0) {
                            strncat(files[index].content, buffer, sizeof(files[index].content) - strlen(files[index].content) - 1);
                            files[index].size += strlen(buffer);
                        }

                        pthread_rwlock_unlock(&files[index].lock);

                        // Release writing status
                        pthread_mutex_lock(&files[index].file_mutex);
                        files[index].is_writing = 0;
                        pthread_mutex_unlock(&files[index].file_mutex);

                        snprintf(buffer, sizeof(buffer), "Write to file '%s' completed.\n", arg1);
                        write(client_socket, buffer, strlen(buffer));
                        show_capability_list(); // Show capability list
                    }
                }
            }
   
        }

        else if (strcmp(command, "mode") == 0) {
            if (strlen(current_user) == 0) {
                snprintf(buffer, sizeof(buffer), "No user has been configured. Please log in first.\n");
            }
            else {
                // Modify file permissions
                int index = find_file(arg1);
                if (index == -1) {
                    snprintf(buffer, sizeof(buffer), "File not found.\n");
                } else if (strcmp(files[index].owner, current_user) != 0) {
                    // Only the file owner can change permissions.
                    snprintf(buffer, sizeof(buffer), "僅檔案擁有者可以更改權限。\n");
                } else {
                    strncpy(files[index].permissions, arg2, sizeof(files[index].permissions) - 1);
                    files[index].permissions[sizeof(files[index].permissions)-1] = '\0';
                    snprintf(buffer, sizeof(buffer), "檔案 %s 的權限已更新為 %s。\n", arg1, arg2);
                    show_capability_list(); // Show capability list
                }
            }
           
        }        
        else {
            snprintf(buffer, sizeof(buffer), "Invaild command。\n");
        }
        //pthread_mutex_unlock(&data_mutex);
        write(client_socket, buffer, strlen(buffer));
       
    }
}

// Define all helper functions globally

void add_user(const char *username, const char *group) {
    // Check if user already exists
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            // If the user already exists, do not create again
            return;
        }
    }

    // Add new user
    strncpy(users[user_count].username, username, sizeof(users[user_count].username) - 1);
    users[user_count].username[sizeof(users[user_count].username)-1] = '\0';
    strncpy(users[user_count].group, group, sizeof(users[user_count].group) - 1);
    users[user_count].group[sizeof(users[user_count].group)-1] = '\0';
    user_count++;
}

int find_file(const char *filename) {
    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i].filename, filename) == 0) {
            return i;
        }
    }
    return -1;
}

int check_permission(const char *username, const File file, char op) {
    // Check if the user is the file owner
    if (strcmp(file.owner, username) == 0) {
        if ((op == 'r' && file.permissions[0] == 'r') || 
            (op == 'w' && file.permissions[1] == 'w')) {
            return 1; // Has permission
        }
        return 0; // No permission
    }

    // Check if the user belongs to the same group as the file
    if (strcmp(file.group, get_user_group(username)) == 0) {
        if ((op == 'r' && file.permissions[2] == 'r') || 
            (op == 'w' && file.permissions[3] == 'w')) {
            return 1; // Has permission
        }
        return 0; // No permission
    }

    // Check others' permissions
    if ((op == 'r' && file.permissions[4] == 'r') || 
        (op == 'w' && file.permissions[5] == 'w')) {
        return 1; // Has permission
    }
    return 0; // No permission
}

const char* get_user_group(const char *username) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            // Return the correct group
            return users[i].group;
        }
    }
    // Return "unknown" if the user is not found
    return "unknown";
}

void send_user_list(int client_socket) {
    char user_list[BUFFER_SIZE] = "";
    strcat(user_list, "=== User List ===\n");
    if (user_count == 0) {
        strcat(user_list, "No users available.\n");
    } else {
        for (int i = 0; i < user_count; i++) {
            char user_entry[50];
            snprintf(user_entry, sizeof(user_entry), "%d. %s (%s)\n", i + 1, users[i].username, users[i].group);
            strcat(user_list, user_entry);
        }
    }
    strcat(user_list, "==================\n");
    write(client_socket, user_list, strlen(user_list));
}

void show_capability_list() {
    printf("\n=== Capability List ===\n");
    if (file_count == 0) {
        printf("No files available.\n");
        return;
    }

    for (int i = 0; i < file_count; i++) {
        printf("%s  %s  %s  %d  %s  %s\n",
               files[i].permissions,
               files[i].owner,
               files[i].group,
               files[i].size,
               files[i].created_at,
               files[i].filename);
    }
    printf("========================\n");
}

void cleanup_files() {
    for (int i = 0; i < file_count; i++) {
        pthread_rwlock_destroy(&files[i].lock);
        pthread_mutex_destroy(&files[i].file_mutex); // Destroy file-specific mutex
        pthread_cond_destroy(&files[i].file_cond);
    }
}

void initialize_large_file() {
    // Confirm there's space to add a file
    if (file_count >= MAX_FILES) {
        printf("無法創建預設檔案，已達到檔案數量上限。\n");
        return;
    }

    // Set file name
    strncpy(files[file_count].filename, "large", sizeof(files[file_count].filename) - 1);
    files[file_count].filename[sizeof(files[file_count].filename) - 1] = '\0';

    // Set permissions
    strncpy(files[file_count].permissions, "rwrwrw", sizeof(files[file_count].permissions) - 1);
    files[file_count].permissions[sizeof(files[file_count].permissions) - 1] = '\0';

    // Set owner
    strncpy(files[file_count].owner, "system", sizeof(files[file_count].owner) - 1);
    files[file_count].owner[sizeof(files[file_count].owner) - 1] = '\0';

    // Set group
    strncpy(files[file_count].group, "AOS", sizeof(files[file_count].group) - 1);
    files[file_count].group[sizeof(files[file_count].group) - 1] = '\0';

    // Fill content with 'A'
    memset(files[file_count].content, 'A', 65536);
    // Ensure it ends with '\0'
    files[file_count].content[65536 - 1] = '\0';
    files[file_count].size = 65536 - 1;

    // Record creation time
    time_t now = time(NULL);
    strftime(files[file_count].created_at, sizeof(files[file_count].created_at), "%Y-%m-%d %H:%M:%S", localtime(&now));

    // Initialize lock
    pthread_rwlock_init(&files[file_count].lock, NULL);
    pthread_mutex_init(&files[file_count].file_mutex, NULL);

    files[file_count].readers = 0;
    atomic_store(&files[file_count].is_writing, 0);

    // Update file count
    file_count++;
    printf("已初始化預設檔案：large_file\n");
}

