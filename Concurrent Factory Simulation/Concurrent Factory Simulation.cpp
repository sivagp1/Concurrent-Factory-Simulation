#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
using namespace std;
using namespace chrono;
using namespace literals::chrono_literals;  // s, h, min, ms, us, ns

// Constants for maximum time for part and product workers
const int MaxTimePart{ 18000 }, MaxTimeProduct{ 20000 };
// Number of iterations for each worker
// Number of part types and product parts
const int num_iterations{ 2 }, part_types{ 5 }, product_parts{ 5 };
// Program start time
auto programStartTime = chrono::system_clock::now();


// Mutexes and condition variables for synchronization
std::condition_variable part_cv, product_cv;
std::mutex buffer_mutex, io_mutex;
std::mutex countermutex;

// Counter for tracking completed products
int counter = 0;

// Vectors for buffer capacity, load time, assembly time, movement time, and initial states
vector<int> bufferCapacity{ 5, 5, 4, 3, 3 };
vector<int> loadTime{ 500, 500, 600, 600, 700 };
vector<int> assemblyTime{ 600, 600, 700, 700, 800 };
vector<int> ToFroTime{ 200, 200, 300, 300, 400 };
vector<int> bufferState{ 0, 0, 0, 0, 0 };  // initially all empty
vector<int> emptyState{ 0, 0, 0, 0, 0 };

// Vector for pickup orders before timeout
vector<int> pickupBeforeTimout;

ofstream Out("log.txt");


//  this is for random order generation time so
int target;
vector<vector<int>> res;
vector<vector<int>> res1;
vector<vector<int>> loadOrdersRes;
vector<vector<int>> pickupOrdersRes;

// Function to generate random order probabilities
vector<int> probablitity(vector<int> temp) {
    int sz = temp.size();
    set<int> randomset;
    vector<int> v;
    v.resize(5);
    while (temp.size() != 0) {
        int random_variable = std::rand();
        int val = random_variable % 5;
        const bool is_in = randomset.find(val) != randomset.end();
        randomset.insert(val);
        if (is_in == false) {
            v[val] = temp.back();
            temp.pop_back();
        }
    }
    return v;
}

// Backtracking function for producer combinations
void backtrackProducers(vector<vector<int>>& res, vector<int>& temp,
    vector<int>& nums, int start, int remain) {
    if (remain < 0)
        return;
    else if (remain == 0) {
        vector<int> modified = probablitity(temp);
        res.push_back(modified);
    }
    else {
        for (int i = start; i < nums.size(); i++) {
            temp.push_back(nums[i]);
            backtrackProducers(res, temp, nums, i, remain - nums[i]);
            temp.pop_back();
        }
    }
}

// Backtracking function for consumer combinations
void backtrackConsumers(vector<vector<int>>& res, vector<int>& temp,
    vector<int>& nums, int start, int remain) {
    if (remain < 0)
        return;
    else if (remain == 0 && (temp.size() == 3 || temp.size() == 2)) {
        vector<int> modified = probablitity(temp);
        res.push_back(modified);
    }
    else {
        for (int i = start; i < nums.size(); i++) {
            temp.push_back(nums[i]);
            backtrackConsumers(res, temp, nums, i, remain - nums[i]);
            temp.pop_back();
        }
    }
}


// Each part worker will produce 5 pieces of all possible combinations, such as
// (2,0,0,2,1), (0,2,0,0,3), (5,0,0,0,0), etc., given that it takes 500, 500,
// 600, 600, 700 microseconds (us) to make each part of type A, B, C, D, E,
// respectively. For example, it takes a part worker 500*2 +600*2+700 us to
// make a (2,0,0,2,1) part combination.

// Function to calculate processing time for a load order
int ProcessTime(const std::vector<int>& loadOrder,
    const std::vector<int>& oldOrder) {
    int time = 0;
    for (int i = 0; i < 5; i++) {
        time += (loadOrder[i] - oldOrder[i]) * loadTime[i];
    }
    return time;
}

// Function to calculate assembly time for a cart state
int calculateAssemblyTime(const std::vector<int>& cartState,
    const std::vector<int>& assemblyTime) {
    int time = 0;
    for (int i = 0; i < 5; i++) {
        time += (cartState[i] * assemblyTime[i]);
    }
    return time;
}

// Function to calculate movement time for a load order
int MovementTime(const std::vector<int>& LoadOrder,
    const std::vector<int>& ToFroTime) {
    int time = 0;
    for (int i = 0; i < 5; i++) {
        time += (LoadOrder[i] * ToFroTime[i]);
    }
    return time;
}

// Function to generate a random valid load order
vector<int> generateRandomValidLoadOrders(
    const std::vector<vector<int>>& loadOrdersRes,
    const std::vector<int>& loadOrder) {
    // randomly re-generate a new load order, which must re-use the previously
    // un-loaded parts (that got moved back).

    int sz = loadOrdersRes.size();
    int index = 0;
    vector<int> indices;  // contains indices of all valid possible orders which
    // follows above condition
    for (int i = 0; i < sz; i++) {
        vector<int> temp = loadOrdersRes[i];
        if (temp[0] >= loadOrder[0] && temp[1] >= loadOrder[1] &&
            temp[2] >= loadOrder[2] && temp[3] >= loadOrder[3] &&
            temp[4] >= loadOrder[4]) {
            indices.push_back(i);
        }
    }

    int goodsz = indices.size();
    int random_variable = std::rand();
    int val = indices[random_variable % goodsz];
    return loadOrdersRes[val];
}

// Function to generate a random valid pickup order
vector<int> generateRandomValidPickupOrders(
    const std::vector<vector<int>>& pickupOrderRes,
    const std::vector<int>& pickupOrder) {
    int sz = pickupOrderRes.size();
    int index = 0;
    vector<int> indices;
    for (int i = 0; i < sz; i++) {
        vector<int> temp = pickupOrderRes[i];
        if (temp[0] >= pickupOrder[0] && temp[1] >= pickupOrder[1] &&
            temp[2] >= pickupOrder[2] && temp[3] >= pickupOrder[3] &&
            temp[4] >= pickupOrder[4]) {
            indices.push_back(i);
        }
    }
    int goodsz = indices.size();
    int random_variable = std::rand();
    int val = indices[random_variable % goodsz];
    return pickupOrderRes[val];
}

// Function to check if a load order can be pushed to the buffer
bool bufferPushable(const std::vector<int>& LoadOrder,
    const std::vector<int>& bufferState,
    const std::vector<int>& BufferCapacity) {
    // if at any index we have a load order non zero and buffer state is less
    // buffer capacity at that index then it's a valid order generated
    for (int i = 0; i < 5; ++i) {
        if ((bufferState[i] < BufferCapacity[i]) && LoadOrder[i] > 0) return true;
    }
    return false;
}

// Function to check if a pickup order can be popped from the buffer
bool bufferPoppable(const std::vector<int>& PickupOrder,
    const std::vector<int>& bufferState,
    const std::vector<int>& BufferCapacity) {
    // if at any index you have a pickup order that is non zero and buffer state
    // at that index is also non zero this is a valid pickup order
    for (int i = 0; i < 5; i++) {
        if (PickupOrder[i] && bufferState[i]) {
            return true;
        }
    }
    return false;
}

void printPreety(string data, vector<int> v) {
    Out << data << ": (" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3]
        << ", " << v[4] << ")" << endl;
}


void logPartWorker(int id, string status, int accumulatedWait, int iteration,
    vector<int> bufferState, vector<int> loadOrder,
    vector<int> updatedBufferState,
    vector<int> updatedLoadOrder,
    std::chrono::time_point<std::chrono::system_clock> t1,
    std::chrono::time_point<std::chrono::system_clock> t2
) {
    std::unique_lock<std::mutex> lock(io_mutex);

    chrono::microseconds T2(accumulatedWait);
    auto now_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        t1 - programStartTime);
    Out << "Current Time: " << now_ms.count() << " us" << endl;
    Out << "Iteration " << iteration << endl;
    Out << "Part Worker ID: " << id << endl;
    Out << "Status: " << status << endl;
    Out << "Accumulated Wait Time: " << T2.count() << " us" << endl;
    printPreety("Buffer State", bufferState);
    printPreety("Load Order", loadOrder);
    printPreety("Updated Buffer State", updatedBufferState);
    printPreety("Updated Load Order", updatedLoadOrder);
    Out << "*******************" << endl;
}


void logProductWorker(int iteration, int id, string status, int accumulated,
    vector<int> bufferstate, vector<int> pickuporder,
    vector<int> localstate, vector<int> cartstate,
    vector<int> updatedbufferstate,
    vector<int> updatedpickuporder,
    vector<int> updatedlocalstate1,
    vector<int> updatedcartstate1, int time,
    vector<int> updatedlocalstate2,
    vector<int> updatedcartstate2, int total, bool flag,
    std::chrono::time_point<std::chrono::system_clock> t1,
    std::chrono::time_point<std::chrono::system_clock> t2,
    bool updateflag

) {

    std::unique_lock<std::mutex> lock(io_mutex);

    if (updateflag) {
        counter++;
    }
    auto now_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        t1 - programStartTime);
    auto now_ms1 = std::chrono::duration_cast<std::chrono::microseconds>(
        t2 - programStartTime);

    Out << "Current Time: " << now_ms.count() << " us" << endl;
    Out << "Iteration " << iteration << endl;
    Out << "Product Worker ID: " << id << endl;
    Out << "Status: " << status << endl;
    //Out << "Accumulated Wait Time: " << T2.count() << " us" << endl;
    printPreety("Buffer State", bufferstate);
    printPreety("PickUpOrder", pickuporder);
    printPreety("Local State", localstate);
    printPreety("Cart State", cartstate);
    printPreety("Updated Buffer State", updatedbufferstate);
    printPreety("Updated Pickup Order", updatedpickuporder);
    printPreety("Local state", updatedlocalstate1);
    printPreety("Cart state", updatedcartstate1);

    if (flag) {
        Out << "Current Time: " << now_ms1.count() << " us" << endl;
        printPreety("Updated Local State", updatedlocalstate2);
        printPreety("Updated Cart State", updatedcartstate2);
    }

    Out << "Total Completed Products: " << counter << endl;
    Out << "*******************" << endl;
}


void PartWorker(int id) {
    auto LoadOrder = emptyState;  // this has be to local
    for (int iteration = 1; iteration <= num_iterations; iteration++) {
        // Each part worker will produce 5 pieces of all possible combinations,
        // such as, 0, 0, 3), ((2, 0, 0, 2, 1), (0, 25, 0, 0, 0, 0), etc., given
        // that it takes 500, 500, 600, 600, 700 microseconds(us) to make each part
        // of type A, B, C, D, E, respectively

        auto newLoadOrder = generateRandomValidLoadOrders(loadOrdersRes, LoadOrder);
        auto tempOldOrder =
            LoadOrder;  // this is just for printing purpose and has no other use
        int processTime = ProcessTime(
            newLoadOrder,
            LoadOrder);  // t1 but there is no previous order in this situation
        LoadOrder = newLoadOrder;
        int movementTime =
            MovementTime(LoadOrder, ToFroTime);  // t2 as per state diagram
        auto T = movementTime + processTime;

        // LOG 1 BEFORE SLEEPING

        // let thread sleep for t1+t2 as in state diagram
        chrono::microseconds processAndMovement(T);
        std::this_thread::sleep_for(processAndMovement);  // THREAD SLEEPNIG HERE

        // The part worker will wait near the buffer area for the buffer space to
        // become available to complete the load order. If the wait time reaches
        // MaxTimePart us (maximum wait time for a part worker), the part worker
        // will stop waiting, move the un-loaded parts back, and randomly
        // re-generate a new load order, which must re-use the previously un-loaded
        // parts (that got moved back).

        auto calT1 = std::chrono::system_clock::now();
        auto calT2 = std::chrono::system_clock::now();

        {  // scope starts here
            auto wait_end_time = std::chrono::system_clock::now() +
                std::chrono::microseconds(MaxTimePart);

            std::unique_lock<std::mutex> lock(
                buffer_mutex);  // take the buffer lock here before opearations

            bool firsttime = true;
            do {
                std::string logtype = firsttime ? "New Load Order" : "Wakeup-Notified";
                // there is no change in buffer state in this case

                // if this load order can't be pushed to buffer than we need to create a
                // new load order
                if (!bufferPushable(LoadOrder, bufferState, bufferCapacity)) {
                    if (!firsttime) continue;
                    auto currentTime = std::chrono::system_clock::now();
                    logPartWorker(id, "New Load Order", 0, iteration, bufferState,
                        LoadOrder, bufferState, LoadOrder, calT1, currentTime);
                    firsttime = false;
                    continue;
                }

                firsttime = false;
                bool emptied = true;
                auto oldLoadOrder = LoadOrder;
                auto oldBufferState = bufferState;

                // all indices of array
                for (int i = 0; i < 5; i++) {
                    // A part worker will load the number of parts of each type to the
                    // buffer, restricted by the buffer’s capacity of each type. For
                    // example, if a load order is (1,1,0,2,1) and the buffer state is
                    // (5,2,1,3,2), then the part worker can place a type B part, and a
                    // type E part to the buffer; thus, the updated load order will be
                    // (1,0,0,2,0) and the updated buffer state will be (5,3,1,3,3). T

                    // this is max we can move but what if load order is 1 and buffer
                    // state is 2 and max capacity is 5
                    int diff = bufferCapacity[i] - bufferState[i];
                    int possiblechange = min(diff, LoadOrder[i]);
                    LoadOrder[i] = LoadOrder[i] - possiblechange;
                    bufferState[i] += possiblechange;
                    if (LoadOrder[i] > 0) emptied = false;
                }

                // LOG3 when part worker wakes up
                auto currentTime = std::chrono::system_clock::now();
                logPartWorker(id, logtype, 0, iteration, oldBufferState, oldLoadOrder,
                    bufferState, LoadOrder, calT1, currentTime);



                product_cv.notify_all();
                if (emptied) {
                    break;
                }

            } while (part_cv.wait_until(lock, wait_end_time) !=
                std::cv_status::timeout);

            // situation when emptied is true means you went and whatever you had ,u
            // unloaded all
            if (LoadOrder != emptyState) {
                int random = 10;
                auto currentTime = std::chrono::system_clock::now();
                logPartWorker(id, "Wakeup-Timeout", random, iteration, bufferState,
                    LoadOrder, bufferState, LoadOrder, calT2, currentTime);
            }

        }  // scope ends here
        // the part worker will go back and will again make a new load order beacuse
        // timeout has happened so in this case the thread sleeps for movement time

        movementTime = MovementTime(LoadOrder, ToFroTime);

        chrono::microseconds T1(movementTime);
        std::this_thread::sleep_for(T1);  // THREAD SLEEPNIG HERE
    }
}


void ProductWorker(int id) {
    auto pickupOrder = emptyState;
    auto cartState = emptyState;
    auto localState = emptyState;
    for (int iteration = 1; iteration <= 5; iteration++) {
        // If the parts moved back after timeout event are (1,1,0,0,0). During the
        // next iteration, if a new pickup order (2,1,2,0,0) is generated, then the
        // real pickup order that the product worker will bring to the buffer area
        // is (1,0,2,0,0), while the parts (1,1,0,0,0), which were brought back
        // during the last iteration due to timeout event, will be staying at local
        // area; (1, 1, 0, 0, 0) will be referred to as local state.

        // the pick up order that has timed out previously to generate a new pick
        // up order but here it is made sure that new pickup orders all values are
        // greater than equal to local state

        pickupOrder = generateRandomValidPickupOrders(pickupOrdersRes, localState);

        // now the pick up order is updated if there was something in local state
        for (int i = 0; i < 5; i++) {
            pickupOrder[i] = pickupOrder[i] - localState[i];
        }


        vector <int> oldBufferState, newBufferState;
        vector <int> oldPickupOrder, newPickupOrder;
        vector <int> oldCartState, newCartState;
        auto calT1 = std::chrono::system_clock::now();
        auto calT2 = std::chrono::system_clock::now();



        // scope starts here and all buffer area locking will go inside this
        {

            auto waitTimeDefined = std::chrono::system_clock::now() +
                std::chrono::microseconds(MaxTimePart);

            std::unique_lock<std::mutex> lock(
                buffer_mutex);  // take the buffer lock here before opearations

            bool firsttime = true;
            do {
                calT1 = std::chrono::system_clock::now();
                oldBufferState = newBufferState = bufferState;
                oldPickupOrder = newPickupOrder = pickupOrder;
                oldCartState = newCartState = cartState;
                std::string logtype =
                    firsttime ? "New Pickup Order" : "Wakeup-Notified";
                // not a good pick up order so create a new one
                if (!bufferPoppable(pickupOrder, bufferState, bufferCapacity)) {
                    if (!firsttime) continue;
                    // log1 product worker
                    auto currentTime = std::chrono::system_clock::now();
                    logProductWorker(iteration, id, logtype, 10, bufferState, pickupOrder,
                        localState, oldCartState, bufferState, pickupOrder,
                        localState, newCartState, 100, localState, cartState,
                        counter, false, calT1, currentTime, false);
                    firsttime = false;
                    continue;
                }

                firsttime = false;

                // If the current buffer state is (4, 0, 2, 1, 3) and a pickup order
                // is(1, 1, 0, 0, 3), then the updated buffer state will be(3, 0, 2, 1,
                // 0) and the updated pickup order will be(0, 1, 0, 0, 0).

                auto oldCartState = cartState, oldBufferState = bufferState,
                    oldPickUpOrder = pickupOrder;



                bool allTaken =
                    true;  // to track if all the entire pick up order was taken care of
                for (int i = 0; i < 5; i++) {
                    int diff = min(pickupOrder[i], bufferState[i]);
                    bufferState[i] -= diff;
                    pickupOrder[i] -= diff;
                    cartState[i] += diff;
                    if (pickupOrder[i] > 0) {
                        allTaken = false;
                    }
                }
                newBufferState = bufferState;
                newPickupOrder = pickupOrder;
                newCartState = cartState;


                part_cv.notify_all();


                // Let us say that all pickup order was taken from buffer area then you
                // first notify producers and move out
                if (allTaken) {
                    pickupBeforeTimout = pickupOrder;
                    break;
                }
                else {

                    auto currentTime = std::chrono::system_clock::now();

                    logProductWorker(iteration, id, "Wakeup-Notfied", 10, oldBufferState,
                        oldPickUpOrder, localState, oldCartState,
                        newBufferState, newPickupOrder, localState, newCartState, 100,
                        localState, cartState, counter, false, calT1, currentTime, false);

                }

            } while (product_cv.wait_until(lock, waitTimeDefined) !=
                std::cv_status::timeout);


            oldBufferState = newBufferState = bufferState;
            oldCartState = newCartState = cartState;
            oldPickupOrder = newPickupOrder = pickupOrder;


            calT2 = std::chrono::system_clock::now();
        }  // scope ends here

        // it has timed out and we have collected some pickup order from buffer area
        // so thread sleeps
        int movementTime = MovementTime(cartState, ToFroTime);
        chrono::microseconds processAndMovement(movementTime);
        std::this_thread::sleep_for(processAndMovement);


        auto oldlocalState = localState;
        oldCartState = cartState;

        for (int i = 0; i < 5; i++) {
            localState[i] += cartState[i];
        }

        cartState = emptyState;

        // what if gathered all things in our pickup order from buffer area and we
        // broke out of loop both assembly time and movement time
        if (pickupOrder == emptyState) {


            std::unique_lock<std::mutex> lock(countermutex);
            auto assemblyTimecalculated =
                calculateAssemblyTime(localState, assemblyTime);
            chrono::microseconds T1(assemblyTimecalculated);
            std::this_thread::sleep_for(T1);  // THREAD SLEEPNIG HERE

            auto currentTime = std::chrono::system_clock::now();

            logProductWorker(iteration, id, "Wakeup-Notified", 10, oldBufferState,
                oldPickupOrder, oldlocalState, oldCartState,
                newBufferState, newPickupOrder, oldlocalState, newCartState, 100,
                emptyState, emptyState, counter, true, calT1, currentTime, true);
            cartState = localState = emptyState;


        }
        else {
            // this leads to error
            auto currentTime = std::chrono::system_clock::now();
            logProductWorker(iteration, id, "Wakeup-Timeout", 10, oldBufferState,
                oldPickupOrder, oldlocalState, oldCartState,
                newBufferState, newPickupOrder, oldlocalState, newCartState, 100,
                localState, cartState, counter, true, calT2, currentTime, false);

        }
    }
}

//main function.
int main() {
    const int m = 20, n = 18;
    // m: number of Part Workers and n is  number of Product Workers

    // my code to generate random vectors for consumers

    target = 5;
    vector<int> nums, temp;
    nums = { 1, 2, 3, 4, 5 };

    // backtrack for Conusmers and producers begins and don't touch this at all
    backtrackProducers(res, temp, nums, 0, target);

    for (int i = 0; i < res.size(); i++) {
        vector<int> vec = res[i];
        // generate it's next permutation and push it
        sort(vec.begin(), vec.end());
        do {
            loadOrdersRes.push_back(vec);
        } while (std::next_permutation(vec.begin(), vec.end()));
    }

    temp.clear();
    backtrackConsumers(res1, temp, nums, 0, target);
    for (int i = 0; i < res1.size(); i++) {
        vector<int> vec = res1[i];
        // generate it's next permutation and push it
        sort(vec.begin(), vec.end());
        do {
            pickupOrdersRes.push_back(vec);
        } while (std::next_permutation(vec.begin(), vec.end()));
    }

    vector<thread> PartW, ProductW;
    for (int i = 0; i < m; ++i) {
        PartW.emplace_back(PartWorker, i + 1);
    }
    for (int i = 0; i < n; ++i) {
        ProductW.emplace_back(ProductWorker, i + 1);
    }
    for (auto& i : PartW) i.join();
    for (auto& i : ProductW) i.join();
    cout << "Finish!" << endl;
}
