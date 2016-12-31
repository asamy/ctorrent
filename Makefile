BIN_DIR = bin
BIN = ctorrent

DEP_DIR = dep
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEP_DIR)/$*.d

CXX = $(CROSS_BUILD)g++
BTYPE = -O3
CXXFLAGS = -std=c++11 $(DEPFLAGS) $(BTYPE) -fopenmp -Wall -Wextra -Wno-deprecated-declarations -Wno-sign-compare -Wno-unused-variable -Wno-unused-parameter -I"." -I"D:\boost_1_60_0"

LIBS = -fopenmp -L"D:\boost_1_60_0\stage\lib" -lboost_system -lboost_filesystem -lboost_program_options -static-libgcc -static-libstdc++ --static
ifeq ($(OS),Windows_NT)
LIBS += -lws2_32 -lshlwapi -lMswsock
else
LIBS += -lpthread -lcurses
endif

OBJ_DIR = obj
SRC = bencode/decoder.cpp bencode/encoder.cpp \
      ctorrent/tracker.cpp ctorrent/peer.cpp ctorrent/torrentmeta.cpp ctorrent/torrentfilemanager.cpp ctorrent/torrent.cpp \
      net/server.cpp net/connection.cpp net/inputmessage.cpp net/outputmessage.cpp \
      util/auxiliar.cpp \
      main.cpp
OBJ = $(SRC:%.cpp=$(OBJ_DIR)/%.o)
DEP = $(SRC:%.cpp=$(DEP_DIR)/%.d)

.PHONY: all clean
.PRECIOUS: $(DEP_DIR)/%.d

all: $(BIN)
clean:
	$(RM) $(OBJ_DIR)/*.o
	$(RM) $(OBJ_DIR)/*/*.o
	$(RM) $(DEP_DIR)/*.d
	$(RM) $(DEP_DIR)/*/*.d
	$(RM) $(BIN)

$(BIN): $(DEP_DIR) $(OBJ_DIR) $(OBJ) $(DEP)
#	@echo "LD   $@"
	$(CXX) -o $@ $(OBJ) $(LIBS)

$(OBJ_DIR)/%.o: %.cpp $(DEP_DIR)/%.d
#	@echo "CXX  $<"
	$(CXX) -c $(CXXFLAGS) -o $@ $<

-include $(DEP)
$(DEP_DIR)/%.d: ;

$(DEP_DIR):
	@mkdir -p $(DEP_DIR)
	@mkdir -p $(DEP_DIR)/bencode
	@mkdir -p $(DEP_DIR)/ctorrent
	@mkdir -p $(DEP_DIR)/net
	@mkdir -p $(DEP_DIR)/util

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/bencode
	@mkdir -p $(OBJ_DIR)/ctorrent
	@mkdir -p $(OBJ_DIR)/net
	@mkdir -p $(OBJ_DIR)/util

