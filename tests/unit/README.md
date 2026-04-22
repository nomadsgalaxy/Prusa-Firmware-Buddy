# How to run unit tests?

### Building Unit Tests
```bash
# create a build folder and run cmake within it
cd ../.. && mkdir build_tests && cd build_tests
cmake .. -G Ninja -DBOARD=BUDDY

# build all the unit tests
ninja tests


> In case you don't have sufficient CMake or Ninja installed, you can use the ones downloaded by build.py/bootstrap.py:
>   ```bash
>   export PATH="$(python ../utils/bootstrap.py --print-dependency-directory cmake)/bin:$PATH"
>   export PATH="$(python ../utils/bootstrap.py --print-dependency-directory ninja):$PATH"
```

> It is recommended to use GCC for compiling unit tests.

### **Running All Tests**
CMake with Catch2 automatically creates a special target that you can build/run with:
```bash
# Using CTest
ctest .

# Using CMake directly
cmake --build . --target test

# Using Ninja
ninja test
```
> Keep in mind tests have to be recompiled after a ch
### **Running Specific Tests**
If you want to run tests with more control, you can use ctest directly.
You can run a specific test or a group of tests using a pattern:

```bash
ctest -R"<test_pattern_name>" --output-on-failure --verbose
```

- `--output-on-failure`: Use this flag to see any output from the test program whenever the test fails.
- `--verbose`: Use this flag to always display the standard output, regardless of whether the test passes or fails.

### **Building with Debug Symbols**

To enable debugging, you must build the tests with debug symbols. This creates a debug-friendly executable.

```bash
cmake .. -G Ninja -DBOARD=BUDDY -DCMAKE_BUILD_TYPE=Debug
```

### **Debugging with GDB**

You can debug tests using GDB, but the approach depends on whether the test has external dependencies:

#### **For simple tests** (no external dependencies):
Run GDB directly from the main project folder:
```
gdb ./build/tests/path/to/test_executable
```
#### **For tests with external dependencies**:

These tests must be run from their executable's directory to properly locate dependencies.
1. Navigate to the executable's directory:

```bash
cd /tests/unit/common/gcode/reader
```

2.  Start GDB and specify the source directory and the test executable:

```bash
gdb -d <path_to_buddy> test_executable
```

> Tip: To make the work with GDB more modern run it with `-tui` flag to provide a nicer interface.

## How to create a new unit test?

1. Create a corresponding directory for it.
    - For example, for a unit in `src/guiapi/src/gui_timer.c` create directory `tests/unit/guiapi/gui_timer`.
2. Store your unittest cases within this directory together with their dependencies.
    Don't use the same file name for testing file and source file. Use '.cpp' extension.
3. Add a CMakeLists.txt with description on how to build your tests.
    - See other unit tests for examples.
    - Don't forget to register any directory you add using `add_subdirectory` in CMakeLists.txt in the same directory.

## Tests on Windows

1. Download & install MinGW and make sure .../MinGW/bin/ is in your path.
2. Check if Python is installed.
3. Download & install some bash (GIT bash could be already installed).
4. Run bash and get to your repository directory (cd ...).
5. Run these to prepare for test:

```bash
mkdir -p build_tests \
&& cd build_tests \
&& rm -rf * \
&& export PATH="$(python ../utils/bootstrap.py --print-dependency-directory cmake)/bin:$PATH" \
&& export PATH="$(python ../utils/bootstrap.py --print-dependency-directory ninja):$PATH" \
&& export CTEST_OUTPUT_ON_FAILURE=1 \
&& cmake .. -G Ninja
```
