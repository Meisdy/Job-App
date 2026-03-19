# Job-App Agent Guidelines

This document provides build, test, and code style guidelines for agentic coding in the Job-App repository.

## 🔧 Build System

### Build Commands

```bash
# Clean build
rm -rf cmake-build-debug && mkdir cmake-build-debug && cd cmake-build-debug
cmake .. && make

# Incremental build
cd cmake-build-debug && make

# Release build (if needed)
cd cmake-build-debug && cmake -DCMAKE_BUILD_TYPE=Release .. && make
```

### Build Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update && sudo apt install -y \
    cmake \
    g++ \
    make \
    libsqlite3-dev \
    libcurl4-openssl-dev
```

**Windows (MSYS2/MinGW):**
```bash
pacman -S --needed \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-gcc \
    mingw-w64-x86_64-curl \
    mingw-w64-x86_64-sqlite3
```

## 🧪 Testing

### Test Structure

The project uses a simple test approach with standalone test files. Tests are located in the `tests/` directory (create if needed).

### Running Tests

```bash
# Run all tests (create test runner first)
cd cmake-build-debug && ctest --output-on-failure

# Run single test
./cmake-build-debug/test_zip_validation
./cmake-build-debug/test_scoring

# Create and run a specific test
cd tests && g++ -std=c++17 -I../include test_zip.cpp -o test_zip && ./test_zip
```

### Test Examples

```cpp
// tests/test_zip.cpp
#include <iostream>
#include <cassert>
#include "../include/db.h"

void test_zip_validation() {
    assert(is_valid_swiss_zip("1000") == true);
    assert(is_valid_swiss_zip("0999") == false);
    assert(is_valid_swiss_zip("A100") == false);
    std::cout << "✅ ZIP validation tests passed\n";
}

int main() {
    test_zip_validation();
    return 0;
}
```

## 🎨 Code Style Guidelines

### General Principles

1. **Do exactly what's needed, no more** - Keep code focused and minimal
2. **Do it well** - Write robust, maintainable code
3. **Single Responsibility** - Each function/class does one thing well
4. **Explicit over implicit** - Make intentions clear

### File Organization

```
include/  - Header files (.h)
src/     - Implementation files (.cpp)
tests/   - Test files (create this directory)
config/  - Configuration files
frontend/- Web interface files
```

### Naming Conventions

**Functions:**
- `camelCase` for regular functions
- `PascalCase` for constructors/types
- Verbs for actions: `getJobs()`, `saveJob()`, `calculateScore()`

**Variables:**
- `camelCase` for local variables
- `snake_case` for member variables (with `m_` prefix optional)
- `UPPER_SNAKE_CASE` for constants

**Types/Classes:**
- `PascalCase` for structs/classes
- `PascalCase` for typedefs/enums

**Examples:**
```cpp
// Good
struct JobRecord {
    std::string jobId;
    int score;
};

std::vector<Job> getUnenrichedJobs(Database& db);
const int MAX_RETRIES = 3;

// Bad
struct job_record {
    std::string JobID;
    int Score;
};

std::vector<job> Get_unenriched_jobs(database& db);
```

### Imports and Includes

**Order:**
1. Standard library headers
2. Third-party library headers  
3. Project headers

**Grouping:**
```cpp
// Standard library
#include <iostream>
#include <vector>
#include <string>

// Third-party
#include <curl/curl.h>
#include <sqlite3.h>

// Project
#include "db.h"
#include "config.h"
```

**Rules:**
- Use angle brackets `<>` for system/library headers
- Use quotes `""` for project headers
- Don't include headers unnecessarily
- Forward declare when possible

### Formatting

**Indentation:**
- 4 spaces (no tabs)
- Consistent indentation for all blocks

**Braces:**
- Opening brace on same line for functions
- Opening brace on new line for classes/structs
- Closing brace on new line

**Good:**
```cpp
void processJob(Job job) {
    if (job.isValid()) {
        saveToDatabase(job);
    }
}

struct JobRecord {
    std::string id;
    int score;
};
```

**Spacing:**
- Space after keywords (`if`, `for`, `while`)
- Space around operators (`=`, `+`, `-`, `==`)
- No space after function names
- Space after commas

**Good:**
```cpp
if (score > threshold && isValid) {
    result = calculateScore(job, config);
}

for (int i = 0; i < jobs.size(); i++) {
    processJob(jobs[i]);
}
```

### Error Handling

**Pattern:**
```cpp
try {
    // Operation that might fail
    auto result = makeApiRequest(url);
    processResult(result);
} catch (const std::exception& e) {
    log(Error, "API request failed: " + std::string(e.what()));
    // Handle error appropriately
    return fallbackValue();
}
```

**Rules:**
- Use specific exception types when possible
- Always catch by const reference
- Include context in error messages
- Don't swallow exceptions silently
- Provide meaningful fallback behavior

**Avoid:**
```cpp
// Bad - too broad
try {
    // ...
} catch (...) {
    // Silent failure
}

// Bad - no context
try {
    // ...
} catch (const std::exception& e) {
    throw; // No context added
}
```

### Types and Modern C++

**Use:**
- `auto` for complex type deductions
- `const` correctness
- Smart pointers (`unique_ptr`, `shared_ptr`) over raw pointers
- Range-based for loops
- `nullptr` over `NULL` or `0`
- `enum class` over plain `enum`

**Examples:**
```cpp
// Good
const auto& jobs = getAllJobs();
for (const auto& job : jobs) {
    processJob(job);
}

std::unique_ptr<Database> db = std::make_unique<SQLiteDatabase>();

// Bad
for (int i = 0; i < jobs.size(); i++) {
    Job job = jobs[i];
    processJob(job);
}

Database* db = new SQLiteDatabase();
```

### Functions

**Rules:**
- Keep functions short (aim for < 20 lines)
- Single responsibility per function
- Pure functions where possible (no side effects)
- Input parameters: `const` and by reference when appropriate
- Return values: prefer returning objects over modifying parameters

**Good:**
```cpp
// Pure function - no side effects
int calculateScore(const Job& job, const Config& config) {
    // ... calculation only
    return score;
}

// Single responsibility
std::vector<std::string> extractSkills(const std::string& jobDescription) {
    // ... extraction only
}
```

**Bad:**
```cpp
// Too many responsibilities
void processAndSaveJob(Job job, Database& db, Logger& log) {
    // Extracts skills
    // Calculates score
    // Validates job
    // Saves to database
    // Logs result
}
```

### Comments and Documentation

**Rules:**
- Code should be self-documenting first
- Comments explain WHY, not WHAT
- Document public interfaces
- Avoid obvious comments

**Good:**
```cpp
// Convert ZIP code to numeric value for Swiss postal codes
// Swiss ZIPs are 4 digits in range 1000-9999
// Returns 0 for invalid ZIP codes
int parseSwissZip(const std::string& zipCode);

// Use exponential backoff for API retries to avoid overwhelming
// the server during temporary outages
void makeApiRequestWithRetry(const std::string& url, int maxRetries);
```

**Bad:**
```cpp
// Increment i by 1
i++;

// Loop through jobs
for (const auto& job : jobs) {
    // Process job
    processJob(job);
}
```

### Database Operations

**Pattern:**
```cpp
// Use RAII for database handles when possible
void withDatabase(const std::function<void(sqlite3*)>& callback) {
    sqlite3* db;
    if (sqlite3_open("jobs.db", &db) != SQLITE_OK) {
        throw std::runtime_error("Failed to open database");
    }
    
    try {
        callback(db);
    } catch (...) {
        sqlite3_close(db);
        throw;
    }
    
    sqlite3_close(db);
}

// Usage
withDatabase([&](sqlite3* db) {
    auto jobs = getAllJobs(db);
    // ... work with jobs
});
```

**Rules:**
- Always check database operation results
- Use transactions for multiple operations
- Close resources properly (RAII pattern)
- Sanitize inputs to prevent SQL injection

### HTTP Operations

**Pattern:**
```cpp
// Unified HTTP client with consistent error handling
std::string httpRequest(
    const std::string& url,
    const std::string& method,
    const std::vector<std::string>& headers = {},
    const std::string& body = ""
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize curl");
    }
    
    // Setup omitted for brevity
    // ...
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        throw std::runtime_error("HTTP request failed: " + std::string(curl_easy_strerror(res)));
    }
    
    return response;
}
```

**Rules:**
- Always initialize curl globalization with `curl_global_init()`
- Check return codes for all curl operations
- Set appropriate timeouts
- Handle SSL properly
- Clean up resources

## 🤖 Agent-Specific Guidelines

### Workflow for Agents

1. **Understand First**: Read relevant files before making changes
2. **Small Changes**: Make incremental, testable changes
3. **Test**: Verify changes compile and work
4. **Document**: Update comments/docs as needed
5. **Clean Up**: Remove temporary files and debug code

### Common Tasks

**Adding a Feature:**
1. Create header file in `include/`
2. Implement in `src/`
3. Add to CMakeLists.txt
4. Write tests
5. Update documentation

**Fixing a Bug:**
1. Reproduce the issue
2. Write test case
3. Fix the code
4. Verify test passes
5. Check for similar issues

### File Patterns

- **Headers**: `.h` files with include guards
- **Implementation**: `.cpp` files matching header names
- **Tests**: `test_*.cpp` in `tests/` directory
- **Config**: JSON files in `config/`

### Debugging Tips

```bash
# Debug compilation
cd cmake-build-debug && make clean && cmake -DCMAKE_BUILD_TYPE=Debug .. && make

# Run with gdb
gdb --args ./Job_App

# Memory checking
valgrind --leak-check=full ./Job_App

# Static analysis
clang-tidy src/*.cpp --fix
```

## 📝 Commit Guidelines

### Commit Messages
- Use imperative mood: "Add feature" not "Added feature"
- First line: short summary (< 50 chars)
- Body: explanation of what and why (if needed)
- Reference issues: "Fixes #123" or "Related to #456"

### Example
```
Add ZIP code validation for Swiss postal codes

- Fixes bug where ZIP codes starting with 0 were incorrectly rejected
- Adds is_valid_swiss_zip() helper function
- Updates scoring logic to use proper validation
- Related to issue #42
```

## 🚀 Deployment

### Local Development
```bash
# Run development server
./cmake-build-debug/Job_App

# Access at http://localhost:8080
```

### Production Considerations
- Use release build (`-DCMAKE_BUILD_TYPE=Release`)
- Set up proper logging
- Configure monitoring
- Set up backups for database
- Implement proper security measures

## 🔍 Code Review Checklist

- [ ] Follows code style guidelines
- [ ] Single responsibility principle
- [ ] Proper error handling
- [ ] No memory leaks
- [ ] Thread safety (if applicable)
- [ ] Tests added/updated
- [ ] Documentation updated
- [ ] No debug code left in
- [ ] Builds without warnings
- [ ] All tests pass

## 📚 Additional Resources

- C++ Core Guidelines: https://isocpp.github.io/CppCoreGuidelines/
- Google C++ Style Guide: https://google.github.io/styleguide/cppguide.html
- SQLite Documentation: https://www.sqlite.org/docs.html
- libcurl Documentation: https://curl.se/libcurl/

---

*Last updated: 2024-03-19*
*Maintainer: Job-App Development Team*
