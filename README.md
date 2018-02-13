# Test task for a C++ developer applicant, [IQ Option](https://iqoption.com)

This project is my implementation of the test task the binary option broker company **IQ Option** offers to its applicants for the **C++ developer** position. It took me three weeks to develop it from scratch, development took place in January-February 2018.

# Task description
The goal was to develop a microservice app using C++ that would build a trader rating based on the following messages received from the system core:
 - **user_registered**(*id*, *name*)
 - **user_renamed**(*id*, *name*)
 - **user_connected**(*id*)
 - **user_disconnected**(*id*)
 - **user_deal_won**(*id*, *amount*, *timestamp*)

The rating is based on the aggregate amount of money won by the traders during the week and is reset on midnight every Monday. Each minute (and upon receiving the *user_connected* message) the service must report the user's rating for every connected user, the rating message consisting of the following data:

 - **Top 10 positions** of the overall trader rating
 - The **user's position** in the rating
 - **10 adjacent positions** before and after the user's one

All the other details, including the choice of transport for communicating with the system core, protocol implementation, data validation, error handling etc, are left at the discretion of the developer.

## System requirements, priorities and other conditions
 - **Linux** should be considered the main target platform, i.e. the code written *must* build and work correctly on that platform.
 - When developing the architecture of the solution, **performance under high load** is to be considered the top priority.
 - The aspect of **restoring the rating between the service re-launches** is beyond the scope of the test task.
 - The use of any **3rd party libraries** is allowed.

# Solution
The following info is the description of the various aspects of the solution and the architectural decisions behind it.

## Folder structure
The project files are structured into several folders based on the purpose of classes/types they implement:

 - **ipc** - modules responsible for the interaction between the service and the calling party. The protocol and transport classes are located in here.
 - **service** - modules implementing the service's business logic. The input data processing, rating calculation and distribution, and the job queue classes are located in here.
 - **utils** - small utility classes (working with date/time, object serialization etc).
 - **lib** - 3rd party library files. The project uses a single such library, *ASIO*, used for the portable implementation of the transport layer based on TCP sockets.
 - **test** - a separate test application designed to emulate the client. Deliberately made very simple and far from perfect in terms of code quality. The app is provided for the project user's convenience, and is completely optional.

## Fundamental architectural decisions
**Portability**

Despite Linux being the target platform, the project's code is written to be as portable as possible. All the modules except the transport layer are written using just C++1z/STL, and for working with sockets I used *ASIO*,  a popular multi-platform library.

**Sockets as a transport**

Sockets were chosen as a base for the transport layer due to their universal and wide-spread nature. At the same moment, the system-dependent logic implementing the transport is isolated within a very limited number of classes used through a thin interface, allowing any other kind of data-transmitting facility (files, pipes etc) to be used as transport with minimal modification of the project code.

**Binary message-based protocol**

The client/service interaction is based on exchanging messages sent in binary format. The message structure is developed with maximum compactness and minimum seriaization/deserialization processing in mind.

## Core structure
Module-wise, the core is composed by the following modules:

 - **Message builder** and **message dispatcher**. They are responsible for parsing the incoming data, turning it into messages and passing it along to the processing modules.
 - **Rating calculator** and **rating announcer**. The calculator processes the incoming messages and builds the rating based on those, while the announcer makes sure to send back the rating messages for all the eligible users.
 - **Job queue** and **worker pool**. Rating announcer and message dispatcher place jobs on the queue, and the worker pool processes those jobs into the messages sent back to the client.

In terms of thread model and inter-thread communication, the core has the following components:

 - The **listener** thread. This is also the main program thread. It wait for the input data to arrive, processes it into messages and puts them into a double buffer later processed by the rating calculator.
 - The **announcer** thread. Once per minute it rotates the buffers filled by the listener thread, performs the rating recalculation and then issues the rating jobs by putting them onto the *job queue*.
 - The **job queue**. Based on several de-facto wait-free multiple-producer single-consumer (MPSC) queues, it is used as a task buffer between the announcer thread (and occasionally the listener one) and the *worker threads*.
 - The **worker threads**. By default there are two of them, but this number can be easily changed. The worker threads process the rating jobs and transform them into actual rating messages which they send to the client.

## Performance
One of the task conditions was to make the service as high performing as possible. To achieve that, the inner data structure has certain redundancy, but that allows the data to be accessed as fast as possible. All the lookup and modification operations are done in amortized constant time, and the rating recalculation used a custom variation of merge sort algorithm that takes into account the specific properties of the rating composition process.

## Build tools
To build the project I've been using CLion IDE, CMake 3.9 build tool bundled with CLion and MinGW-w64 toolchain. Basically, the project can be build on any platform which is supported by the ASIO/gcc/CMake bundle.

## Launch and use
To launch the service, pass it a port number it should listen via a command-line argument, e.g.:

> IQOptionTestTask 40000

You could use *test* app as a client, or you could write your own client using the protocol message classes from the file *./ipc/protocol.h*.
