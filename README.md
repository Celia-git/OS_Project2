# OS_Project2
kernel level programming

Phase 1: Alarm Clock Implementation
(Deliverable: Fully functional timer_sleep avoiding busy-waiting)
•	Task 1.1: Implement thread blocking in timer_sleep() and place threads on waitlist.
•	Task 1.2: Implement timer interrupt handler to wake threads at scheduled time.
•	Task 1.3: Update data structures in threads/thread.h and devices/timer.c to support alarm functionality.
•	Task 1.4: Independently write and run alarm clock-specific unit tests validating blocking and waking behavior.
________________________________________
Phase 2: Priority Scheduler Implementation
(Deliverable: Scheduler that runs highest-priority ready thread and enforces preemption)
•	Task 2.1: Modify ready list to keep threads sorted by priority.
•	Task 2.2: Implement preemption and forced yielding on arrival of higher-priority threads.
•	Task 2.3: Update scheduling and context-switching code for priority-based decisions.
•	Task 2.4: Create and execute tests verifying scheduler prioritization and correct context transitions.
________________________________________
Phase 3: Priority Donation Implementation
(Deliverable: Priority donation mechanism handling nested and multiple donations)
•	Task 3.1: Extend lock structures to track priority donation state.
•	Task 3.2: Implement nested/multiple priority donation propagation with max nesting depth.
•	Task 3.3: Adjust thread priority accessors to incorporate effective priority (including donation).
•	Task 3.4: Develop tests for simple, nested, and chained priority donation scenarios.
________________________________________
Phase 4: Thread Priority Interface
(Deliverable: Fully functional thread priority setter and getter considering donation)
•	Task 4.1: Implement thread_set_priority() that sets thread priority and yields if needed.
•	Task 4.2: Implement thread_get_priority() returning effective priority with donations applied.
•	Task 4.3: Write tests to validate dynamic priority changes and thread yielding behavior.
________________________________________
Phase 5: Final Testing and Documentation
(Deliverable: Complete system testing and finalized design document)
•	Task 5.1: Run all comprehensive Pintos tests (make check) and debug failures.
•	Task 5.2: Prepare detailed test reports for alarm clock, scheduler, donation, and priority changes.
•	Task 5.3: Complete the design document describing all major design choices, data structures, and algorithms.
•	Task 5.4: Prepare final submission package including full source tree, design document, and test grade screenshots.

