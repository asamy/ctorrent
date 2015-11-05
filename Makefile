BIN_DIR = bin
BIN = $(BIN_DIR)/ctorrent

CXX = g++
BTYPE = -g3 -ggdb3 -O0 -D_DEBUG
CXXFLAGS = -std=c++11 $(BTYPE) -fopenmp -Wall -Wextra -Wno-sign-compare -Wno-unused-variable -Wno-unused-parameter -I"."

LIBS = -fopenmp -lcrypto
ifeq ($(OS),Windows_NT)
LIBS += -lboost_system-mt -lws2_32 -lshlwapi
else
LIBS += -lboost_system -lpthread
endif

OBJ_DIR = obj
SRC = bencode/decoder.cpp bencode/encoder.cpp \
	ctorrent/tracker.cpp ctorrent/peer.cpp ctorrent/torrent.cpp \
	net/connection.cpp net/inputmessage.cpp net/outputmessage.cpp \
	util/auxiliar.cpp \
	main.cpp

OBJ = $(SRC:%.cpp=$(OBJ_DIR)/%.o)

.PHONY: all clean

all: $(BIN)
clean:
	$(RM) $(OBJ_DIR)/*.o
	$(RM) $(OBJ_DIR)/*/*.o
	$(RM) $(BIN)

$(BIN): $(OBJ_DIR) $(BIN_DIR) $(OBJ)
	@echo "LD   $@"
	@$(CXX) -o $@ $(OBJ) $(LIBS)

$(OBJ_DIR)/%.o: %.cpp
	@echo "CXX  $<"
	@$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/bencode
	@mkdir -p $(OBJ_DIR)/ctorrent
	@mkdir -p $(OBJ_DIR)/net
	@mkdir -p $(OBJ_DIR)/util
