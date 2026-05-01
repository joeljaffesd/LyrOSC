BUILD_DIR = build
TARGET    = vocal-stem-stt
CMAKE     ?= cmake

.PHONY: all clean rebuild

all: $(BUILD_DIR)/Makefile
	$(CMAKE) --build $(BUILD_DIR) --target $(TARGET) -j$$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
	@echo "Build complete: ./$(TARGET)"

$(BUILD_DIR)/Makefile:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

rebuild: clean all
