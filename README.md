# Problem 017 - Train Ticket Management System

**ACMOJ Problem ID**: 1170

## Table of Contents

- [Problem 017 - Train Ticket Management System](#problem-017---train-ticket-management-system)
  - [Table of Contents](#table-of-contents)
  - [Overview](#overview)
  - [Grading Criteria](#grading-criteria)
  - [Interface Specification](#interface-specification)
  - [Q&A](#qa)
  - [Test Data](#test-data)
  - [Per-Testcase Resource Limits](#per-testcase-resource-limits)
  - [Submission Requirements](#submission-requirements)
    - [OJ Git Repository Compilation Process](#oj-git-repository-compilation-process)
    - [Git Configuration Requirements](#git-configuration-requirements)
    - [Submission Guidelines](#submission-guidelines)
    - [Resource Limits](#resource-limits)
    - [Evaluation Notes](#evaluation-notes)
    - [Academic Integrity](#academic-integrity)

## Overview

#### Implementation Content

This assignment requires implementing a train ticket booking system similar to 12306. The system must store user data, ticket data, and train data **locally** and perform efficient operations on them.

#### Program Structure

The backend program must implement the interfaces required in the documentation. After receiving a complete command, it should immediately output the result and return to the state of waiting for input. Use C++ for implementation.

This assignment does not allow the use of external libraries, STL containers (except `std::string`), or the `algorithm` library.


## Grading Criteria

- Performance - 80%

  Memory usage will be strictly limited in all tests. The program may be opened and closed multiple times during testing.

  You can create up to 20 files in the directory where the executable is located.

  There are two types of tests: correctness testing and stress testing.

- Code Review - 20%






## Interface Specification

#### User Related

A user has the following data:

- `username`: The unique identifier of the user, a string starting with a letter and composed of letters, digits, and underscores, with a length not exceeding 20.
- `password`: A string composed of letters, digits, and underscores, with a length between 6 and 30.
- `name`: A string composed of two to five Chinese characters.
- `mailAddr`: Follows general email address format, containing only digits, uppercase/lowercase letters, `@` and `.`, with a length not exceeding 30. (No need to verify email format validity)
- `privilege`: User group priority level, an integer from 0 to 10.

#### Train Related

A train has the following attributes:

- `trainID`: The unique identifier of the train, a string starting with a letter and composed of letters, digits, and underscores, with a length not exceeding 20.

- `stationNum`: The number of stations the train passes through, an integer greater than 1 and not exceeding 100.

- `stations`: All station names the train passes through, `stationNum` items in total. Station names are composed of Chinese characters, not exceeding 10 characters.

  Format: `station_1 station_2 station_3 ... station_stationNum`

- `seatNum`: The number of seats on the train, an integer not exceeding 100000.

- `prices`: Ticket prices between each pair of stations, `(stationNum - 1)` items in total. The `i`-th item represents the price from `station_i` to `station_(i+1)`, an integer not exceeding 100000.

  Format: `price_1 price_2 price_3 ... price_(stationNum - 1)`
  
- `startTime`: The daily departure time of the train, in `hr:mi` format. For example: `23:51`.
  
- `travelTimes`: Travel time between each pair of stations (in minutes), `(stationNum - 1)` items in total, each being an integer not exceeding 10000.

- `stopoverTimes`: Stopover time at each station (in minutes) except the starting and terminal stations, `(stationNum - 2)` items in total, each being an integer not exceeding 10000.
  
- `saleDate`: The sale date range (closed interval) of the train. Within this time range, the same train runs every day. 2 items in total, each being a day from June to August 2021, in `mm-dd` format. For example: `06-07`.
  
- `type`: Train type, a single uppercase letter.

#### Commands

Each command has a "usage frequency" parameter, representing its usage frequency in actual situations and its approximate proportion in stress tests. It is divided into the following levels, which you can use as a basis for designing algorithms.

- SF: Super frequently used. (~1000000)
- F: Frequently used. (~100000)
- N: Normally used. (~10000)
- R: Rarely used. (~100)

Parameter format is `-<key> <argument>`, where `<key>` is a lowercase letter. To simplify the description, `<argument>` is omitted in the parameter list and referred to as `-<key>` in the description. Unless otherwise specified, each parameter is a string without spaces.

The order of parameters in the parameter list is not restricted. Optional parameters can be omitted. Default values for optional parameters (if not empty) are provided in the description.

The return value of a command is the execution result output to standard output in command-line interaction.

- [N] `add_user`

  - Parameter List

    `-c -u -p -n -m -g`

  - Description

    The current user `<cur_username>` is `-c`, creating a user with `<username>` as `-u`, `<password>` as `-p`, `<name>` as `-n`, `<mailAddr>` as `-m`, and `<privilege>` as `-g`.

    Permission requirement: `-c` is logged in, and the new user's privilege is lower than `-c`'s privilege.

    Specially, when creating the first user, the new user's privilege is `10`, not subject to the above permission rules. Specifically, when creating the first user, ignore the `-c` and `-g` parameters and set the new user's priority to `10`.

  - Return Value

    Registration successful: `0`

    Registration failed: `-1`

- [F] `login`

  - Parameter List

    `-u -p`

  - Description

    Log in with `-u` as `<username>` and `-p` as `<password>`. If login is successful, add this user to the current user list. The current user list is empty when first entering the system.

  - Return Value

    Login successful: `0`

    Login failed: `-1`
  
- [F] `logout`

  - Parameter List

    `-u`

  - Description

    The user with `<username>` as `-u` logs out. If logout is successful, remove this user from the current user list.

  - Return Value

    Logout successful: `0`

    Logout failed: `-1`

- [SF] `query_profile`

  - Parameter List

    `-c -u`

  - Description

    The user with `<username>` as `-c` queries the information of the user with `<username>` as `-u`.

    Permission requirement: `-c` is logged in, and "the privilege of `-c` is greater than the privilege of `-u`, or `-c` and `-u` are the same".

  - Return Value

    Query successful: One line string, containing the queried user's `<username>`, `<name>`, `<mailAddr>`, and `<privilege>` in order, separated by spaces.

    Query failed: `-1`

- [F] `modify_profile`

  - Parameter List

    `-c -u (-p) (-n) (-m) (-g)`

  - Description

    The user with `<username>` as `-c` modifies the information of the user with `<username>` as `-u`. Modification parameters are the same as registration parameters, and all can be omitted.

    Permission requirement: `-c` is logged in, and "the privilege of `-c` is greater than the privilege of `-u`, or `-c` and `-u` are the same", and `-g` must be lower than `-c`'s privilege.

  - Return Value

    Modification successful: Return the modified user's information, in the same format as `query_profile`.
  
    Modification failed: `-1`



- [N] `add_train`
  - Parameter List
  
    `-i -n -m -s -p -x -t -o -d -y`
  
  - Description
  
    Add a train with `<trainID>` as `-i`, `<stationNum>` as `-n`, `<seatNum>` as `-m`, `<stations>` as `-s`, `<prices>` as `-p`, `<startTime>` as `-x`, `<travelTimes>` as `-t`, `<stopoverTimes>` as `-o`, `<saleDate>` as `-d`, and `<type>` as `-y`.
    
    Since `-s`, `-p`, `-t`, `-o`, and `-d` consist of multiple values, separate two values with `|` when inputting (still a string without spaces).
    
  - Return Value
  
    Add successful: `0`
  
    Add failed: `-1`
    
  - Example:
  
    `> add_train -i HAPPY_TRAIN -n 3 -m 1000 -s 上院|中院|下院 -p 114|514 -x 19:19 -t 600|600 -o 5 -d 06-01|08-17 -y G`
    
    `0`
  
- [N] `release_train`

  - Parameter List

    `-i`

  - Description

    Release the train with `<trainID>` as `-i`. Trains before release can be deleted and tickets cannot be sold; trains after release cannot be deleted and tickets can be sold.

  - Return Value

    Release successful: `0`

    Release failed: `-1`

  - Example:

    `> release_train -i HAPPY_TRAIN `

    `0` 

- [N] `query_train`

  - Parameter List

    `-i -d`

  - Description

    Query the situation of the train with `<trainID>` as `-i` departing on date `-d`, where `-d` is in `mm-dd` format.

  - Return Value

    Query successful: Output `(<stationNum> + 1)` lines in total.

    The first line is `<trainID> <type>`

    The next `<stationNum>` lines, the `i`-th line is `<stations[i]> <ARRIVING_TIME> -> <LEAVING_TIME> <PRICE> <SEAT>`, where `<ARRIVING_TIME>` and `<LEAVING_TIME>` are the absolute times the train arrives at and leaves this station, in `mm-dd hr:mi` format. `<PRICE>` is the cumulative ticket price from the starting station to this station, and `<SEAT>` is the number of remaining tickets from this station to the next station. For the arrival time at the starting station and the departure time at the terminal station, all digits are replaced with `x`; the remaining tickets at the terminal station are also replaced with `x`.
    
    Query failed: `-1`
    
  - Example

    (Following the example of adding a train)

    `> query_train -d 07-01 -i HAPPY_TRAIN`
    
    `HAPPY_TRAIN G`
    
    `上院 xx-xx xx:xx -> 07-01 19:19 0 1000`
    
    `中院 07-02 05:19 -> 07-02 05:24 114 1000`
    
    `下院 07-02 15:24 -> xx-xx xx:xx 628 x`

- [N] `delete_train`

  - Parameter List

    `-i`

  - Description

    Delete the train with `<trainID>` as `-i`.

  - Return Value

    Delete successful: `0`

    Delete failed: `-1`



- [SF] `query_ticket`

  - Parameter List

    `-s -t -d (-p time)`

  - Description

    Query tickets departing from `-s` and arriving at `-t` on date `-d`. Note: This date is the date when the train departs from `-s`, not the date when the train departs from the starting station.
    
    The value of `-p` is one of `time` and `cost`. If it is `time`, the output is sorted by the time required for the train from smallest to largest; otherwise, it is sorted by ticket price from lowest to highest.

  - Return Value

    Query successful: The first line outputs an integer representing the number of trains that meet the requirements.

    The following lines each output a train that meets the requirements, sorted as required. The format is `<trainID> <FROM> <LEAVING_TIME> -> <TO> <ARRIVING_TIME> <PRICE> <SEAT>`, where the departure time and arrival time formats are the same as `query_train`, `<FROM>` and `<TO>` are the departure and arrival stations, `<PRICE>` is the cumulative price, and `<SEAT>` is the maximum number of tickets that can be purchased.
    
    Query failed: `-1`

  - Example

    (Following the example of querying trains)

    `> query_ticket -s 中院 -t 下院 -d 08-17`
    
    `1`
    
    `HAPPY_TRAIN 中院 08-17 05:24 -> 下院 08-17 15:24 514 1000`

- [N] `query_transfer`

  Parameter list and meaning are the same as `query_ticket`.

  - Description

    Query trains that meet the conditions in the case of exactly one transfer, only output the optimal solution.

  - Return Value

  	Query failed (no train meets the requirements): `0`
  	
  	Query successful: Output 2 lines, the two trains taken in the transfer, in the same format as `query_ticket`.

- [SF] `buy_ticket`
  
  - Parameter List
    `-u -i -d -n -f -t (-q false)`
    
  - Description
    
    The user with `<username>` as `-u` purchases: `<trainID>` as `-i`, date as `-d`, from station `-f` to station `-t`, `-n` tickets.
    
    `-q` can be `false` or `true`. If it is `true`, it means that in the case of **insufficient tickets**, willing to accept standby ticket purchase. When there are tickets available, it is **immediately** regarded as this user purchasing the ticket.
    
    Permission requirement: `-u` is logged in.
    
  - Return Value
  
    Purchase successful: An integer representing the total price of the order.
    
    Join standby: `queue`
    
    Purchase failed: `-1`
    
  - Example
  
    (Following the example of querying tickets, assuming users are logged in)
  
    `> buy_ticket -u Texas -i HAPPY_TRAIN -d 08-17 -n 800 -f 中院 -t 下院`
  
    `411200`
  
    `> buy_ticket -u Lappland -i HAPPY_TRAIN -d 08-16 -n 500 -f 上院 -t 下院 -q true`
  
    `queue`
    
  - Example Explanation
  
    User `Texas` purchased 800 train tickets departing from 中院 on 8.17. In the `add_train` operation above, the train departing from 上院 on 8.16 will arrive at 中院 on 8.17, so in fact the train `Lappland` is trying to buy tickets for is the same as `Texas`'s, so `Lappland` cannot buy 500 tickets.
  
- [F] `query_order`

  - Parameter List

    `-u`

  - Description

    Query all order information of the user with `<username>` as `-u`, sorted by transaction time from newest to oldest (even if a standby order is successfully ticketed, the transaction time is based on the time of placing the order).

    Permission requirement: `-u` is logged in.

  - Return Value

    Query successful: The first line outputs an integer representing the number of orders.

    The following lines each represent an order, in the format `[<STATUS>] <trainID> <FROM> <LEAVING_TIME> -> <TO> <ARRIVING_TIME> <PRICE> <NUM>`, where `<NUM>` is the number of tickets purchased, and `<STATUS>` represents the status of the order. Possible values are: `success` (ticket purchase successful), `pending` (in the standby ticket queue), and `refunded` (already refunded).
    
    Query failed: `-1`
    
  - Example

    `> query_order -u Lappland`

    `1`

    `[pending] HAPPY_TRAIN 上院 08-17 05:24 -> 下院 08-17 15:24 628 500`

- [N] `refund_ticket`

  - Parameter List

    `-u (-n 1)`

  - Description

    The user with `<username>` as `-u` refunds the `-n`-th order from newest to oldest (i.e., the return order of `query_order`).

    Permission requirement: `-u` is logged in.

  - Return Value

    Refund successful: `0`

    Refund failed: `-1`

- [R] `clean`

  - Parameter List

    None

  - Description

    Clear all data.

  - Return Value

    `0`

- [R] `exit`

  - Parameter List

    None

  - Description

    Exit the program, all online users log out.

  - Return Value

    `bye`


## Q&A

- Q: Are there trains that depart on different days?

   A: Yes, and the sale date range refers to the date when **the train departs from the starting station**, while querying or purchasing tickets refers to the date when **the train departs from the boarding station**.

- Q: Do I need to check if the **format** of various information is legal when reading?

   A: No, the input format is guaranteed to be legal.

- Q: Is the priority of the standby ticket queue that the first order is satisfied first, and if it cannot be satisfied, check if the next one can be satisfied?

   A: Yes.

- Q: Can users with the same privilege level modify each other?

   A: No, see the `modify_profile` permission description (the privilege of `-c` is greater than the privilege of `-u`, or `-c` and `-u` are the same).

- Q: If there are only 49 tickets left and you want to buy 50 tickets, will all 50 tickets in this order enter the standby queue?

   A: Yes, orders are the minimum unit of ticket purchase and standby, and both will only be **fully satisfied**.

- Q: When refunding the n-th order, is the serial number n the serial number including refunded orders?

   A: Yes, it is equivalent to the n-th order output by `query_order`.

- Q: Does `exit` really exit the program or just log out online users?

   A: Exit the program and all online users log out.

- Q: Does a train pass through a station multiple times?

   A: No.

- Q: Can a transfer be from the same train to the same train?

   A: No.

- Q: Does the optimal solution of `query_transfer` refer to the time optimal solution when `-p` is `time`, and the price optimal solution when `-p` is `cost`?

    A: Yes.

- Q: Should the train be displayed when there are no tickets available when querying tickets?

    A: Yes.
    
- Q: Can trains be queried before release, and can they be queried and ticketed after release?

    A: Before release, trains can: query trains, delete trains (no ticket query); after release, any operation except deleting trains can be performed.

- Q: In the backend interface, multiple users are allowed to log in to the system. So in the frontend web implementation, should multiple users log in at the same time? Or according to the normal account convention, only one user can log in at a time?

    A: Each frontend client can log in one user, and many frontends can be opened at the same time, and their login status can coexist (this is why the current user list is a set rather than a specific user).
    For the backend, different frontends send commands with different `-c` or `-u`, so for a frontend, you only need to determine which [one] user is currently logged in, and tell the backend this information as the operator of the instruction.

- Q: Is it required that `-c` is logged in when it is not the first user?

    A: Yes, the privilege of the new user should be [lower than] the current user rather than [not higher than].

- Q: Is there a limit on the number of stations a train passes through?

    A: Yes, at least two stations (starting station and terminal station), and at most 100 stations.

- Q: Is there a limit on the length of train running time?

    A: Yes, no more than 3 days.

- Q: When will `add_train` fail?

    A: When there is a `trainID` conflict.

- Q: Will adding a user fail if the id conflicts?

    A: Yes.

- Q: Is it guaranteed that all input (referring to the input of all operations) dates are a certain day from June to August 2021?

    A: The time is guaranteed to be legal and within the 2021 range.

- Q: Should the operation fail when `query_order` is executed when `-u` is not logged in?

    A: Yes.

- Q: What are the main forms of command failure?

    A: Mainly key-value conflicts when adding, invalid input (such as the input station does not exist, etc.) or not meeting permission requirements.

- Q: What is the return value when `query_ticket` and `query_order` fail?

    A: `-1`

- Q: Evaluation submission method?

    A: Submit via git, and provide Makefile or CMakeLists.txt in the repository root directory.

- Q: Is there a limit on the privilege that can be modified in `modify_profile`?

    A: Yes, it needs to be lower than the privilege of `-c`.

- Q: What does the time in `buy_ticket` refer to?

    A: Consistent with the query, it refers to the boarding time rather than the time the train departs from the starting station.

- Q: For trains with only two stations, what is the -o parameter?

    A: The `-o` parameter content is an underscore (`-o _`)

- Q: Will `query_ticket` fail?

    A: No. If the station does not exist, it is not considered a query failure, but is considered to have no trains that meet the conditions, so it returns `0`.

- Q: When querying tickets, if the time and ticket price are the same when sorting, what should I do?

    A: Use `trainID` lexicographic order as the second sorting key. If `query_transfer` has tied results, try to choose the plan with less riding time on train 1 (the train passing through the starting station).

- Q: If the number of tickets purchased is greater than the maximum number of seats on the train, can you still wait?

    A: No.
    
- Q: Will repeated login fail?

    A: Yes, return `-1`.
    
- Q: What is the ticket situation when `query_train` is executed before the train is released?

    A: Before the train is released, `query_train` can find the corresponding ticket, and none of them have been sold.
    
- Q: Can you buy 0 tickets?

    A: No.
    
- Q: Can the number of train seats be 0?

    A: No.





## Test Data

Test data is located in the `./data/` directory. Note that this data may not be the evaluation data!

## Per-Testcase Resource Limits

- **Time Limit (per test case)**: 5000 ms (min), 40000 ms (max)
- **Memory Limit (per test case)**: 42 MiB (min), 47 MiB (max)
- **Disk Usage**: 366 MiB (min), 488 MiB (max)
- **File Count Limit**: 20 files

## Submission Requirements

### OJ Git Repository Compilation Process

For Git compilation, we will first clone the repository using a command similar to:
```bash
git clone <repo_url> . --depth 1 --recurse-submodules --shallow-submodules --no-local
```

Then we check if there is a `CMakeLists.txt` file. If it exists, we run (if not, a warning message will be displayed):
```bash
cmake .
```

Finally, we check if there is any of `GNUmakefile`/`makefile`/`Makefile` (if cmake was run previously, this will be the generated Makefile). If it exists, we run (if not, a warning message will be displayed):
```bash
make
```

After this process is complete, we will use the `code` file in the project root directory as the compilation result.

The project does not provide a CMakeLists.txt file, so you need to create and edit it yourself. The local environment has gcc-13 and g++-13 available.

### Git Configuration Requirements

**IMPORTANT**: You must create a `.gitignore` file in your project root directory to avoid OJ evaluation conflicts.

The `.gitignore` file should include at least the following entries:

```gitignore
CMakeFiles/
CMakeCache.txt
```

### Submission Guidelines

- The submitted code must be able to compile successfully through the above compilation process
- The compiled executable file name must be `code`
- The program needs to be able to read data from standard input and write results to standard output
- Please ensure the code runs correctly within the given time and space limits
- **You must use C++ language** to implement this assignment
- Cannot use external libraries
- Cannot use STL containers (except `std::string`)
- Cannot use `algorithm` library

### Evaluation Notes

- The evaluation system will test your program using the provided test data
- The program output must exactly match the expected output (including format)
- Exceeding time or memory limits will be judged as the corresponding error type
- Please pay attention to the overall time performance of your code and the time complexity of each part of your algorithm

### Academic Integrity

If any violations are found during evaluation or code review (including but not limited to using unconventional methods to pass test cases), your final score may be significantly reduced or become **0 points**.