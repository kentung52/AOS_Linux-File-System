# 定義編譯器和選項
CC = gcc

LDFLAGS = -pthread

# 定義目標程式名稱
TARGET = server

# 定義源文件
SRCS = server.c 

# 預設目標
all: $(TARGET)

# 生成執行檔
$(TARGET):
	$(CC) -w -o $(TARGET) $(SRCS) $(LDFLAGS)

# 清理執行檔
clean:
	rm -f $(TARGET)

