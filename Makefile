CXX = g++
CXXFLAGS = -std=c++17 -Iinclude -Wall -Wno-deprecated-declarations
LDFLAGS = -lresolv -lpqxx  -lpq -MMD -MP

SRC_DIR = src
OBJ_DIR = obj
BIN = pigeonX

SRC = $(wildcard $(SRC_DIR)/*.cpp)
OBJ = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRC))

$(BIN): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(BIN)
