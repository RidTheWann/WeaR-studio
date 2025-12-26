# Contributing to WeaR Studio

First off, thank you for considering contributing to WeaR Studio! 

This document provides guidelines and steps for contributing to the project.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [Making Changes](#making-changes)
- [Coding Standards](#coding-standards)
- [Pull Request Process](#pull-request-process)
- [Reporting Bugs](#reporting-bugs)
- [Feature Requests](#feature-requests)

---

## Code of Conduct

By participating in this project, you agree to maintain a respectful and inclusive environment. Please:

- Be respectful and considerate in your communication
- Welcome newcomers and help them get started
- Focus on what is best for the community
- Accept constructive criticism gracefully

---

## Getting Started

### Prerequisites

Before contributing, ensure you have:

- Visual Studio 2022 (or later) with C++ workload
- Qt 6.10.1 (MSVC 2022 64-bit)
- FFmpeg 6.0+ development libraries
- CMake 3.21+
- Git

### Fork and Clone

1. Fork the repository on GitHub
2. Clone your fork locally:

```bash
git clone https://github.com/YOUR-USERNAME/WeaR-studio.git
cd WeaR-studio
git remote add upstream https://github.com/RidTheWann/WeaR-studio.git
```

---

## Development Setup

### Build the Project

```powershell
# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.10.1/msvc2022_64" `
      -DFFMPEG_ROOT="C:/ffmpeg"

# Build Debug
cmake --build build --config Debug

# Copy FFmpeg DLLs
Copy-Item C:/ffmpeg/bin/*.dll -Destination build/bin/Debug/
```

### Run Tests

```powershell
cd build
ctest --config Debug
```

---

## Making Changes

### Branch Naming

Use descriptive branch names:

- `feature/add-audio-mixer` — New features
- `fix/encoder-crash` — Bug fixes
- `docs/update-readme` — Documentation updates
- `refactor/scene-manager` — Code refactoring

### Commit Messages

Follow the conventional commits format:

```
<type>(<scope>): <description>

[optional body]

[optional footer]
```

**Types:**
- `feat` — New feature
- `fix` — Bug fix
- `docs` — Documentation changes
- `style` — Code style changes (formatting, etc.)
- `refactor` — Code refactoring
- `test` — Adding tests
- `chore` — Maintenance tasks

**Examples:**
```
feat(encoder): add AMD AMF encoder support
fix(capture): resolve memory leak in frame pool
docs(readme): update build instructions for Qt 6.10
```

---

## Coding Standards

### C++ Style Guide

We follow modern C++20 standards:

```cpp
// Naming conventions
namespace WeaR {                    // PascalCase for namespaces
class MyClassName {                 // PascalCase for classes
    void myMethod();                // camelCase for methods
    int m_memberVariable;           // m_ prefix for members
    static int s_staticVar;         // s_ prefix for static members
};
}

// Use smart pointers
std::unique_ptr<Object> obj = std::make_unique<Object>();

// Use range-based for loops
for (const auto& item : items) { }

// Use [[nodiscard]] for functions returning important values
[[nodiscard]] int calculate() const;

// Use auto sparingly, prefer explicit types for clarity
auto iterator = container.begin();  // OK
int count = getCount();             // Prefer explicit type
```

### Qt Style Guide

```cpp
// Use Q_OBJECT macro for QObject subclasses
class MyWidget : public QWidget {
    Q_OBJECT
public:
    explicit MyWidget(QWidget* parent = nullptr);
    
signals:
    void dataChanged();
    
private slots:
    void onButtonClicked();
};

// Connect signals using new syntax
connect(button, &QPushButton::clicked, this, &MyWidget::onButtonClicked);
```

### File Organization

- Header files: `.h`
- Source files: `.cpp`
- One class per file (exceptions for closely related small classes)
- Include guards using `#pragma once`

---

## Pull Request Process

### Before Submitting

1. **Sync with upstream:**
   ```bash
   git fetch upstream
   git rebase upstream/main
   ```

2. **Build and test:**
   ```powershell
   cmake --build build --config Debug
   ```

3. **Check code style:**
   - No trailing whitespace
   - Consistent indentation (4 spaces)
   - No compiler warnings

### Submitting

1. Push your branch to your fork
2. Open a Pull Request against `main`
3. Fill in the PR template:
   - Description of changes
   - Related issue (if any)
   - Screenshots (for UI changes)
   - Testing performed

### Review Process

- At least one maintainer review is required
- Address review comments promptly
- Keep PRs focused — one feature/fix per PR
- Large changes should be discussed in an issue first

---

## Reporting Bugs

### Before Submitting

- Check existing issues to avoid duplicates
- Try to reproduce with the latest version
- Gather system information

### Bug Report Template

```markdown
## Description
A clear description of the bug.

## Steps to Reproduce
1. Go to '...'
2. Click on '...'
3. See error

## Expected Behavior
What should happen.

## Actual Behavior
What actually happens.

## Environment
- WeaR Studio version: 
- Windows version: 
- GPU: 
- Qt version: 
- FFmpeg version: 

## Screenshots/Logs
If applicable, add screenshots or log output.
```

---

## Feature Requests

We welcome feature suggestions! Please:

1. Check existing issues for similar requests
2. Open a new issue with the `enhancement` label
3. Describe the feature and its use case
4. Be open to discussion about implementation

### Feature Request Template

```markdown
## Feature Description
A clear description of the feature.

## Use Case
Why is this feature needed?

## Proposed Solution
How should it work?

## Alternatives Considered
Any alternative solutions you've considered.
```

---

## Areas for Contribution

Looking for something to work on? Here are priority areas:

### High Priority
- Audio capture and mixing (WASAPI)
- Local recording support
- Unit tests

### Medium Priority
- GPU shader effects
- Scene transitions
- Browser source plugin

### Good First Issues
- Documentation improvements
- Bug fixes
- Code cleanup

---

## Questions?

Feel free to:
- Open an issue for questions
- Join discussions in existing issues
- Contact the maintainer

---

Thank you for contributing to WeaR Studio!
