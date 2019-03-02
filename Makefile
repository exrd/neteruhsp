


CXX=clang++
CXXFLAGS= -O2 -std=c++11

LDFLAGS=

TARGET=./bin/neteruhsp

INCFLAGS=

SRC_DIR=./neteruhsp
SRCS= $(wildcard $(SRC_DIR)/*.cc)

OBJ_DIR=./obj
OBJS= $(SRCS:$(SRC_DIR)/%.cc=$(OBJ_DIR)/%.o)


$(TARGET): $(OBJS)
	mkdir -p ./bin
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cc
	mkdir -p ./obj
	$(CXX) $(CXXFLAGS) -c $< -o $@ $(INCFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)

