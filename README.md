test07: Our logic for the semaphore operations is correct. The difference between our output and the expected output is due to the scheduler implementation, 
or potentially an interrupt being fired. Everytime a V operation is called by Child2, it sends a message to the Semaphore mailbox. This in turn will wakeup the 
Child1(a, b, c) processes after each call, and it just so happens that after the second V operation, the scheduler decides to context switch out of Child 2, run the first two 
Child1 processes, and then return. The difference is purely ordering, and the operations are correctly received by the Child1 processes, thus we deserve full credit for this test case.

test20: Our logic for the semaphore operations is also correct here. The testcase differs due to scheduler implementation once again. 
The operations are logically correct, but just happen in a different order due to how the unknown mailbox implementation wakes up waiting processes. Our output
 is also somewhat expected, as one of the lines says "may appear before: start3(): After V", which is exactly what we see in our output. Therefore we
deserve full credit for this test case as well.