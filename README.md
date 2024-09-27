# Hash-Forge

Hash-Forge is a C++ tool designed to generate and optimize hash functions through random mutations and scoring mechanisms. It leverages various statistical tests to evolve hash functions that exhibit desirable properties such as low collision rates and good avalanche characteristics.

## Table of Contents

- [Features](#features)
- [Installation](#installation)
- [Usage](#usage)
- [How It Works](#how-it-works)
- [Tests Implemented](#tests-implemented)
- [Contributing](#contributing)
- [License](#license)

## Features

- **Automated Hash Function Generation**: Generates random hash functions using a virtual instruction set.
- **Mutation and Evolution**: Applies random mutations to evolve hash functions over time.
- **Statistical Testing**: Implements multiple tests to score and evaluate the quality of hash functions.
- **Multi-threaded Execution**: Utilizes parallel processing to speed up the search for optimal hash functions.
- **Just-In-Time Compilation**: Uses AsmJit for JIT compilation of hash functions for performance evaluation.

## Installation

### Prerequisites

- C++17 or higher
- [AsmJit](https://github.com/asmjit/asmjit) library
- [OpenMP](https://www.openmp.org/) for multi-threading support
- C++ Standard Library headers

### Steps

1. **Clone the Repository**

   ```bash
   git clone https://github.com/yourusername/hash-forge.git
   ```

2. **Navigate to the Project Directory**

   ```bash
   cd hash-forge
   ```

3. **Install Dependencies**

   Ensure that AsmJit and other dependencies are installed and accessible to your compiler.

4. **Build the Project**

   ```bash
   g++ -std=c++17 -O2 -fopenmp main.cpp -o hash-forge
   ```

   Adjust the compiler flags as necessary for your environment.

## Usage

Run the executable to start the hash function generation and optimization process:

```bash
./hash-forge
```

The program will output the best-performing hash functions it discovers, along with their scores and statistics.

## How It Works

Hash-Forge operates by initializing a random hash function composed of a series of virtual instructions (`VHash`). It then enters a loop where it:

1. **Mutates** the current best hash function to create new candidate functions.
2. **Scores** each candidate using a combination of tests (e.g., collision tests, avalanche tests).
3. **Selects** the best-performing candidate to become the new current best.
4. **Repeats** the process to continually improve the hash function.

### Virtual Instructions

Hash functions are represented as a series of instructions operating on virtual registers. Supported operations include:

- Arithmetic (`Add`, `Subtract`, `Multiply`)
- Bitwise (`Xor`, `Or`, `And`, `Not`)
- Bit Shifts (`ShiftLeft`, `ShiftRight`)
- Rotations (`RotateLeft`, `RotateRight`)

### Multi-threading

The program uses OpenMP to parallelize the mutation and evaluation of hash functions across multiple threads, significantly speeding up the optimization process.

## Tests Implemented

### Duplicate Test

Checks for hash collisions by hashing a set of input values and counting duplicates.

### Avalanche Test

Evaluates the avalanche effect by flipping individual bits in the input and measuring the impact on the output hash.

### Recursive Test

Tests the hash function's behavior when its output is fed back as input.

### VW Test

A custom test that ensures the hash function doesn't produce repeating patterns over a large number of iterations.

## Contributing

Contributions are welcome! Please open an issue or submit a pull request on GitHub.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
