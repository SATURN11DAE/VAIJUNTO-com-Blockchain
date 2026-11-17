CXX      := g++
# O backlog padrao do httplib e 5: rajadas de conexoes simultaneas (teste em massa) tomam "connection reset".
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pthread -Iheaders -Ibibliotecas -DCPPHTTPLIB_LISTEN_BACKLOG=512
LDFLAGS  := -pthread

TARGET     := vaijunto_no
CLI_TARGET := vaijunto_cli
BUILD      := build

SRCS := source/main.cpp \
	source/util.cpp \
	source/io.cpp \
	source/protocoloConsorcio.cpp \
	source/protocoloCliente.cpp \
	source/terminalServidor.cpp

CLI_SRCS := source/terminalCliente.cpp

OBJS     := $(patsubst source/%.cpp,$(BUILD)/%.o,$(SRCS))
CLI_OBJS := $(patsubst source/%.cpp,$(BUILD)/%.o,$(CLI_SRCS))

all: $(TARGET) $(CLI_TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(CLI_TARGET): $(CLI_OBJS)
	$(CXX) $(CLI_OBJS) -o $(CLI_TARGET) $(LDFLAGS)

$(BUILD)/%.o: source/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD) $(TARGET) $(CLI_TARGET)

clean-ledgers:
	rm -rf ledgers

.PHONY: all clean clean-ledgers
