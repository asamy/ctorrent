BIN_DIR = bin
BIN = $(BIN_DIR)/ctorrent

DEP_DIR = dep
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEP_DIR)/$*.d

CXX = g++
BTYPE = -g3 -ggdb3 -O0 -D_DEBUG
CXXFLAGS = -std=c++11 $(DEPFLAGS) $(BTYPE) -fopenmp -Wall -Wextra -Wno-sign-compare -Wno-unused-variable -Wno-unused-parameter -I"."

LIBS = -fopenmp -lboost_system-mt -lboost_program_options-mt 
ifeq ($(OS),Windows_NT)
LIBS += -lws2_32 -lshlwapi
else
LIBS += -lpthread
endif

OBJ_DIR = obj
SRC = bencode/decoder.cpp bencode/encoder.cpp \
	ctorrent/tracker.cpp ctorrent/peer.cpp ctorrent/torrent.cpp \
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

$(BIN): $(DEP_DIR) $(OBJ_DIR) $(BIN_DIR) $(OBJ) $(DEP)
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

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/bencode
	@mkdir -p $(OBJ_DIR)/ctorrent
	@mkdir -p $(OBJ_DIR)/net
	@mkdir -p $(OBJ_DIR)/util

