#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <termios.h>

#define PORT 12500
#define BUFFER_SIZE (512 * 1024)
#define MAX_USERS 20

typedef struct {
    char username[20];
    char group[20];
} User;

User users[MAX_USERS];
int user_count = 0;
char current_user[20] = ""; // Track the currently selected user
char current_group[20] = ""; // Track the group of the selected user

void initial_menu(int client_socket);
void user_menu(int client_socket);
void send_command(int client_socket, const char *command);
void list_users(int client_socket);
void set_non_canonical_mode();
void reset_terminal_mode();


// Added function: read until newline or EOF, used for simple responses to general commands
void read_until_newline_or_eof(int client_socket) {
    char buf[BUFFER_SIZE];
    memset(buf, 0, sizeof(buf));
    int bytes_read = read(client_socket, buf, sizeof(buf) - 1);
    if (bytes_read > 0) {
        buf[bytes_read] = '\0';
        // Directly print the received data, assuming the server appends '\n' at the end of the response
        printf("Server: %s", buf);
    } else {
        printf("Server disconnected or no data.\n");
    }
}

// Used to read the file contents from the "read" command until it encounters <END_OF_FILE>
void read_until_end_of_file(int client_socket) {
    char read_buf[BUFFER_SIZE];
    printf("Server: ");
    while (1) {
        int bytes_read = read(client_socket, read_buf, sizeof(read_buf) - 1);
        if (bytes_read <= 0) {
            // Connection interrupted or no data
            break;
        }
        read_buf[bytes_read] = '\0';

        // Check if the END marker is included
        char *end_marker = strstr(read_buf, "<END_OF_FILE>");
        if (end_marker != NULL) {
            // Truncate everything after the END marker
            *end_marker = '\0';
            printf("%s", read_buf);
            printf("\n"); // New line after finishing
            break; // End of reading loop
        } else {
            // The END marker has not appeared yet, so print it directly
            printf("%s", read_buf);
        }
    }
}

int main() {
    int client_socket;
    struct sockaddr_in server_address;

    // Create socket
    if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);

    // Convert IPv4 address from text to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr) <= 0) {
        perror("Invalid address/Address not supported");
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(client_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to the server.\n");

    // Initial menu to create/select users
    initial_menu(client_socket);

    close(client_socket);
    return 0;
}

void initial_menu(int client_socket) {
    char command[BUFFER_SIZE] = {0};
    int choice;

    while (1) {
        // Clear the user list
        user_count = 0;
        memset(users, 0, sizeof(users));

        // List current users
        list_users(client_socket);

        printf("\nPlease choose an option:\n");
        printf("1. create_user\n");
        for (int i = 0; i < user_count; i++) {
            printf("%d. %s (%s)\n", i + 2, users[i].username, users[i].group);
        }
        printf("0. exit\n");
        printf("Enter your choice: ");

        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Please enter a number.\n");
            while (getchar() != '\n'); // Clear input buffer
            continue;
        }
        getchar(); // Clear newline character

        if (choice == 0) {
            printf("Exiting.\n");
            exit(0);
        }
        else if (choice == 1) {
            char username[20], group[20];
            printf("Enter username: ");
            if (scanf("%19s", username) != 1) {
                printf("Invalid username input.\n");
                while (getchar() != '\n'); // Clear input buffer
                continue;
            }
            printf("Enter group: ");
            if (scanf("%19s", group) != 1) {
                printf("Invalid group input.\n");
                while (getchar() != '\n');
                continue;
            }
            getchar();
            snprintf(command, BUFFER_SIZE, "create_user %s %s", username, group);
            send_command(client_socket, command);
            read_until_newline_or_eof(client_socket);
        }
        else if (choice >= 2 && choice < 2 + user_count) {
            strncpy(current_user, users[choice - 2].username, sizeof(current_user) - 1);
            current_user[sizeof(current_user) - 1] = '\0';
            strncpy(current_group, users[choice - 2].group, sizeof(current_group) - 1);
            current_group[sizeof(current_group) - 1] = '\0';

            snprintf(command, BUFFER_SIZE, "set_user %s", current_user);
            send_command(client_socket, command);
            char buffer[BUFFER_SIZE];
            memset(buffer, 0, sizeof(buffer));
            int bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                printf("Server: %s", buffer);
            }

            // Check if login is successful
            if (strstr(buffer, "User:") != NULL) {
                user_menu(client_socket);
            } else {
                memset(current_user, 0, sizeof(current_user));
                memset(current_group, 0, sizeof(current_group));
            }
        } else {
            printf("Invalid choice. Please try again.\n");
        }
    }
}

void user_menu(int client_socket) {
    char command[BUFFER_SIZE] = {0};

    while (1) {
        printf("\nUser: %s (%s)\n", current_user, current_group);
        printf("Available commands:\n");
        printf("1. create <filename> <permissions>\n");
        printf("2. read <filename>\n");
        printf("3. write <filename> o/a\n");
        printf("4. mode <filename> <permissions>\n");
        printf("5. exit\n");
        printf("Enter command: ");

        char input[BUFFER_SIZE] = {0};
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("Invalid input. Please try again.\n");
            continue;
        }
        input[strcspn(input, "\n")] = '\0';

        // Special handling for the write command
        char cmd[10];
        char filename[50];
        char mode[2];
        if (sscanf(input, "%s %s %s", cmd, filename, mode) == 3 && strcmp(cmd, "write") == 0) {
            set_non_canonical_mode();
            snprintf(command, BUFFER_SIZE, "write %s %s", filename, mode);
            send_command(client_socket, command);

            // Wait for server confirmation
            char buffer[BUFFER_SIZE] = {0};
            int bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                printf("Server: %s\n", buffer);
            }

            // If it is ready to write
            if (strstr(buffer, "Ready to write") != NULL) {
                printf("Enter content to write: ");
                char content[BUFFER_SIZE] = {0};
                if (fgets(content, sizeof(content), stdin) == NULL) {
                    printf("Failed to read content. Aborting write command.\n");
                    reset_terminal_mode();
                    continue;
                }
                content[strcspn(content, "\n")] = '\0';

                send_command(client_socket, content);
                reset_terminal_mode();

                // Receive notification of completion
                memset(buffer, 0, sizeof(buffer));
                bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    printf("Server: %s\n", buffer);
                }
            }
            continue;
        }

        // Special handling for the read command (repeated reads until <END_OF_FILE>)
        if (strncmp(input, "read ", 5) == 0) {
            send_command(client_socket, input);
            read_until_end_of_file(client_socket);
            continue;
        }

        // show_capability_list or other brief response commands
        if (strcmp(input, "show_capability_list") == 0 || strcmp(input, "5") == 0) {
            send_command(client_socket, input);
            read_until_newline_or_eof(client_socket);
            continue;
        }

        if (strcmp(input, "exit") == 0 || strcmp(input, "6") == 0) {
            printf("Returning to main menu.\n");
            break;
        }

        // Other general commands
        snprintf(command, sizeof(command), "%s", input);
        send_command(client_socket, command);
        // Use read_until_newline_or_eof to read the entire line of response
        read_until_newline_or_eof(client_socket);
    }
}

void send_command(int client_socket, const char *command) {
    send(client_socket, command, strlen(command), 0);
}

void list_users(int client_socket) {
    char command[BUFFER_SIZE] = "list_users";
    send_command(client_socket, command);

    char buffer[BUFFER_SIZE] = {0};
    int bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("%s", buffer);

        // Parse user list
        char *line = strtok(buffer, "\n");
        user_count = 0;
        while (line != NULL) {
            if (strncmp(line, "=== User List ===", 17) == 0 ||
                strncmp(line, "==================", 17) == 0 ||
                strncmp(line, "No users available.", 19) == 0) {
                // Skip
            } else {
                char username[20], group[20];
                if (sscanf(line, "%*d. %19[^ (] (%19[^)])", username, group) == 2) {
                    strncpy(users[user_count].username, username, sizeof(users[user_count].username) - 1);
                    users[user_count].username[sizeof(users[user_count].username) - 1] = '\0';
                    strncpy(users[user_count].group, group, sizeof(users[user_count].group) - 1);
                    users[user_count].group[sizeof(users[user_count].group) - 1] = '\0';
                    user_count++;
                }
            }
            line = strtok(NULL, "\n");
        }
    } else {
        printf("Failed to read user list from server.\n");
    }
}

void set_non_canonical_mode() {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag &= ~(ICANON); // Disable line buffering
    t.c_cc[VMIN] = 1;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

void reset_terminal_mode() {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag |= ICANON;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

