#ifndef __SPMC_LOCKFREE_QUEUE__
#define __SPMC_LOCKFREE_QUEUE__

#include <vector>
#include <cstdint>
#include <utility>
#include <atomic>
#include <thread>
#include <chrono>
#include "botan_all.h"

/*
Single-producer, multi-consumer lock-free queue
Assumptions:
    - no consumers are spawned until the queue contains at least an item, otherwise consumers would segfault while resolving first node address
    - data chunks contained in the node items are read-only

- nextNode pointer can have values SPMC_QUEUE_WAIT (starting value), and then nullptr or actual address, and it's updated only by the producer, and polled by consumers
- done atomic integer keeps track of how many consumers have already processed each item, it's initialized to 0, and atomically incremented by consumers
*/

// https://stackoverflow.com/questions/31978324/what-exactly-is-stdatomic

constexpr auto SPMC_QUEUE_BSIZE = 32000000;
constexpr uint64_t SPMC_QUEUE_WAIT = -1;

constexpr int SPMC_QUEUE_POLLING_INTERVAL_MS = 50;

std::atomic<int16_t> Spmc_Queue_Size;
constexpr int SPMC_MAX_QUEUE_SIZE = 20; // used to enforce a limit on the currently produced blocks in order to wait for consumers to deallocate some

struct Spmc_Queue_Node {
    std::vector<uint8_t> data;
    Spmc_Queue_Node* nextNode; // values: 0 (a.k.a. nullptr), SPMC_QUEUE_WAIT (placeholder for not yet available), or actual pointer address
    std::atomic<uint8_t> done; // up to 255 consumers
    // use uint8_t reread = done.fetch_add(1) , or done += 1 or done++ to increment

    Spmc_Queue_Node(std::vector<uint8_t> data_) :
        data(std::move(data_)),
        nextNode((Spmc_Queue_Node*)(void*)(SPMC_QUEUE_WAIT)),
        done(0) {}
};

// typedef void (*consumer_task)(const void* p1, const void* p2); // TODO customize as needed

/*void producer_fn(IDescriptor& inputFile) {

    ssize_t readBytes;
    Spmc_Queue_Node** curNext;

    std::vector<uint8_t> v(SPMC_QUEUE_BSIZE);
    readBytes = inputFile.readTill(&v[0],SPMC_QUEUE_BSIZE);
    if(readBytes <= 0) return; // TODO if the input file is empty, for hashing we have to produce one single packet containing an empty std::vector, with nextNode already equal to nullptr
    v.resize(readBytes);

    Spmc_Queue_Node* rootNode = new Spmc_Queue_Node(v);
    curNext = &(rootNode->nextNode);

    // TODO spawn consumer threads, pass as params: rootNode, N (total number of consumers)

    for(;;) {
        // v has been std::moved in node constructor, so now it is empty
        v.resize(SPMC_QUEUE_BSIZE);
        readBytes = inputFile.readTill(&v[0],SPMC_QUEUE_BSIZE);
        if(readBytes <= 0) {
            *curNext = nullptr; // signal consumers that there is not next block so they can finish their tasks and return
            break;
        }
        v.resize(readBytes);
        while(Spmc_Queue_Size >= SPMC_MAX_QUEUE_SIZE)
            std::this_thread::sleep_for(std::chrono::milliseconds(SPMC_QUEUE_POLLING_INTERVAL_MS)); // wait for some items to be deallocated
        rootNode = new Spmc_Queue_Node(v);
        *curNext = rootNode;
        curNext = &(rootNode->nextNode);
        Spmc_Queue_Size++; // fetch_add
    }

    // TODO join consumer threads
}*/

/*void consumer_fn(Spmc_Queue_Node* rootNode, uint8_t totalConsumers, consumer_task taskfn) {
    Spmc_Queue_Node* curNode = rootNode;
    for(;;) {
        auto& v = curNode->data;
        auto& n = curNode->done;
        taskfn(v); // TODO customize
        uint8_t reread = ++n; // fetch_add + 1

        uint64_t nnd;
        for(;;) {
            nnd = (uint64_t)(void*)(curNode->nextNode);
            if(nnd == SPMC_QUEUE_WAIT) {
                std::this_thread::sleep_for(std::chrono::milliseconds(SPMC_QUEUE_POLLING_INTERVAL_MS));
                continue;
            }
            else if (nnd == 0) { // nullptr
                if(reread == totalConsumers) { // we are the last consumer having processed the last block (by construction, no one else will access that block anymore), deallocate it and exit
                    delete curNode;
                    Spmc_Queue_Size--; // fetch_sub
                }
                return; // (if reread != totalConsumers) last block, but not last consumer to process, someone else will deallocate it
            }
            else { // nnd has an actual pointer address, so break out of this inner loop and continue with next iteration of the outer one
                Spmc_Queue_Node* oldNode = curNode;
                curNode = curNode->nextNode;
                if(reread == totalConsumers) {
                    delete oldNode;
                    Spmc_Queue_Size--; // fetch_sub
                }
                break;
            }
        }
    }
}*/

// TODO test in CLI mode only first, when args are only files, not dirs (for dirs, use the standard sequential method)
// TODO generalize Spmc_Queue_Node structure in order to allow ConsumerTask to perform directory hashing (e.g. add a field with relative path, or packets containing only relpathnames, see how rh dir hasher already works)
class ConsumerTask {
public:
    virtual ~ConsumerTask() = default;
    Spmc_Queue_Node* curNode;
    const uint8_t totalConsumers;

    ConsumerTask(Spmc_Queue_Node* rootNode, uint8_t totalConsumers_) :
        curNode(rootNode), totalConsumers(totalConsumers_) {}

    virtual void processItem() = 0;

    void run() {
        for(;;) {
            auto& n = curNode->done;
            processItem();
            uint8_t reread = ++n; // fetch_add + 1

            uint64_t nnd;
            for(;;) {
                nnd = (uint64_t)(void*)(curNode->nextNode);
                if(nnd == SPMC_QUEUE_WAIT) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(SPMC_QUEUE_POLLING_INTERVAL_MS));
                    continue;
                }
                else if (nnd == 0) { // nullptr
                    if(reread == totalConsumers) { // we are the last consumer having processed the last block (by construction, no one else will access that block anymore), deallocate it and exit
                        delete curNode;
                        Spmc_Queue_Size--; // fetch_sub
                    }
                    return; // (if reread != totalConsumers) last block, but not last consumer to process, someone else will deallocate it
                }
                else { // nnd has an actual pointer address, so break out of this inner loop and continue with next iteration of the outer one
                    Spmc_Queue_Node* oldNode = curNode;
                    curNode = curNode->nextNode;
                    if(reread == totalConsumers) {
                        delete oldNode;
                        Spmc_Queue_Size--; // fetch_sub
                    }
                    break;
                }
            }
        }
    }
};

class FileWriterConsumer : public ConsumerTask {
public:
    IDescriptor& outputFile;
    ProgressHook* progressHook;
    // TODO either open output file in caller, or pass path here and open in constructor using fdfactory
    FileWriterConsumer(Spmc_Queue_Node* rootNode_, uint8_t totalConsumers_, IDescriptor& outputFile_, ProgressHook* progressHook_ = nullptr) :
        ConsumerTask(rootNode_, totalConsumers_), outputFile(outputFile_), progressHook(progressHook_) {}

    void processItem() override { // TODO this should be a writeTill, in order to gracefully exit thread in case of disk full / disk quota exceeded errors
        auto& v = curNode->data;
        auto s = v.size();
        outputFile.writeAll(&v[0], s);
        if(progressHook != nullptr) progressHook->publishDelta(s);
    }
};

class HasherConsumer : public ConsumerTask {
public:
    const std::string hashAlgo;
    std::shared_ptr<Botan::HashFunction> hash1;
    ProgressHook* progressHook;
    HasherConsumer(Spmc_Queue_Node* rootNode_, uint8_t totalConsumers_, std::string hashAlgo_, ProgressHook* progressHook_ = nullptr) :
        ConsumerTask(rootNode_, totalConsumers_), hashAlgo(hashAlgo_), progressHook(progressHook_) {
        hash1 = Botan::HashFunction::create(hashAlgo);
    }

    void processItem() override {
        auto& v = curNode->data;
        auto s = v.size();
        hash1->update(&v[0], s);
        if(progressHook != nullptr) progressHook->publishDelta(s);
    }

    // output hash is finalized in caller
};

template<typename STR>
std::vector<uint8_t> producer_hasher(const STR& inputFilePath, const uint8_t algo, ProgressHook* progressHook = nullptr) {
    auto&& fd = fdfactory.create(inputFilePath,FileOpenMode::READ);
    if(!fd) return rh_errorHash;

    ssize_t readBytes;
    Spmc_Queue_Node** curNext;

    std::vector<uint8_t> v(SPMC_QUEUE_BSIZE);
    readBytes = fd.readTill(&v[0],SPMC_QUEUE_BSIZE);
    if(readBytes < 0) return rh_errorHash;
    else if(readBytes == 0) {
        // hash of empty file
        auto hash1 = Botan::HashFunction::create(rh_hashLabels[algo]);
        auto result = hash1->final(); // hash of empty file
        return std::vector<uint8_t>(result.data(),result.data()+result.size());
    }
    v.resize(readBytes);

    Spmc_Queue_Node* rootNode = new Spmc_Queue_Node(v);
    curNext = &(rootNode->nextNode);
    Spmc_Queue_Size = 1;

    // spawn consumer threads, pass as params: rootNode, N (total number of consumers)
    HasherConsumer consumer(rootNode, 1, rh_hashLabels[algo], progressHook);
    std::thread consThread(&HasherConsumer::run, consumer);

    for(;;) {
        // v has been std::moved in node constructor, so now it is empty
        v.resize(SPMC_QUEUE_BSIZE);
        readBytes = fd.readTill(&v[0],SPMC_QUEUE_BSIZE);
        if(readBytes <= 0) {
            *curNext = nullptr; // signal consumers that there is not next block so they can finish their tasks and return
            break;
        }
        v.resize(readBytes);
        while(Spmc_Queue_Size >= SPMC_MAX_QUEUE_SIZE)
            std::this_thread::sleep_for(std::chrono::milliseconds(SPMC_QUEUE_POLLING_INTERVAL_MS)); // wait for some items to be deallocated
        rootNode = new Spmc_Queue_Node(v);
        *curNext = rootNode;
        curNext = &(rootNode->nextNode);
        Spmc_Queue_Size++; // fetch_add
    }
    fd.close();

    // join consumer threads
    consThread.join();
    // PRINTUNIFIED("Queue size at the end was %d\n",Spmc_Queue_Size.load());
    // Spmc_Queue_Size = 0;

    // collect hash
    auto result = consumer.hash1->final();
    return std::vector<uint8_t>(result.data(),result.data()+result.size());
}

// algo can be not provided (have to pass pointer uint8_t*), and in that case hasher consumer must not be instantiated
template<typename STR>
int producer_createAndHash(const STR& outputFilePath,
                            const uint8_t* algo,
                            std::string& output_hash /* output param, valued only if algo is not null */,
                            uint64_t size,
                            const std::string& seed,
                            const std::string& backendCipher,
                            ProgressHook* progressHook = nullptr) {
    auto&& fd = fdfactory.create(outputFilePath,FileOpenMode::XCL);
    if(!fd) return -1; // TODO caller should provide already-open file

    // auto progressHook = getProgressHookP(size); // TODO who must publish progress? writer consumer or hasher one? maybe better writer one
    size_t written=0,consumed=0;
    constexpr unsigned halfblockSize = SPMC_QUEUE_BSIZE/2;
    const unsigned keySize = backendCipher == "AES-128/CTR" ? 16 : 32;

    Spmc_Queue_Node** curNext;
    
    // create first chunk
    std::vector<uint8_t> v(SPMC_QUEUE_BSIZE); // inout in createRandomFile
    uint8_t* p1 = &v[0];
    uint8_t* p2 = p1+halfblockSize;
    
    // init with supplied seed or with system entropy
    if(seed.empty()) {
        botan_rng_t rng{};
        botan_rng_init(&rng, nullptr);
        botan_rng_get(rng,p1,keySize);
    }
    else {
        botan_hash_t hash1{};
        botan_hash_init(&hash1,"SHA-256",0);
        botan_hash_update(hash1,(uint8_t*)(seed.c_str()),seed.size());
        botan_hash_final(hash1,p1);
    }
    
    // expansion
    botan_cipher_t enc{};
#ifdef __aarch64__
    const char* defaultBackendCipher = "ChaCha";
#else
    const char* defaultBackendCipher = has_aes_hw_instructions() ? "AES-256/CTR" : "ChaCha";
#endif
    const char* usedCipher = backendCipher.empty() ? defaultBackendCipher : backendCipher.c_str();
    botan_cipher_init(&enc, usedCipher, Botan::ENCRYPTION);
    // botan_cipher_init(&enc, "AES-256/CTR", Botan::ENCRYPTION);
    // botan_cipher_init(&enc, "ChaCha", Botan::ENCRYPTION);
    // botan_cipher_init(&enc, "SHACAL2/CTR", Botan::ENCRYPTION);
    PRINTUNIFIED("Using %s as underlying cipher for PRNG\n", usedCipher);
    
    uint64_t remainingToGenerate = size;
    uint64_t remainder = size % SPMC_QUEUE_BSIZE;
    
    // fill first chunk
    // ------------------------------------
    botan_cipher_set_key(enc, p1, keySize);
    botan_cipher_start(enc, nullptr, 0);
    botan_cipher_update(enc, BOTAN_CIPHER_UPDATE_FLAG_FINAL, p2, halfblockSize, &written, p1, halfblockSize, &consumed);

    botan_cipher_set_key(enc, p2, keySize);
    botan_cipher_start(enc, nullptr, 0);
    botan_cipher_update(enc, BOTAN_CIPHER_UPDATE_FLAG_FINAL, p1, halfblockSize, &written, p2, halfblockSize, &consumed);
    // ------------------------------------
    v.resize(remainder);
    remainingToGenerate -= remainder;

    Spmc_Queue_Node* rootNode = new Spmc_Queue_Node(v);
    curNext = &(rootNode->nextNode);
    Spmc_Queue_Size = 1;

    // spawn consumer threads, pass as params: rootNode, N (total number of consumers)
	uint8_t algo1 = algo != nullptr ? *algo : 0;
	size_t totalThreads = algo != nullptr ? 2 : 1;
    HasherConsumer consumer(rootNode, totalThreads, rh_hashLabels[algo1]);
    std::unique_ptr<std::thread> hasherThread;
	if(algo != nullptr) hasherThread.reset(new std::thread(&HasherConsumer::run, consumer));

    FileWriterConsumer consumerW(rootNode, totalThreads, fd, progressHook);
	std::thread writerThread(&FileWriterConsumer::run, consumerW);

    while(remainingToGenerate > 0) {
        // v has been std::moved in node constructor, so now it is empty
        v.resize(SPMC_QUEUE_BSIZE);
		p1 = &v[0];
		p2 = p1 + halfblockSize;
        // ------------------------------------
        botan_cipher_set_key(enc, p1, keySize);
        botan_cipher_start(enc, nullptr, 0);
        botan_cipher_update(enc, BOTAN_CIPHER_UPDATE_FLAG_FINAL, p2, halfblockSize, &written, p1, halfblockSize, &consumed);

        botan_cipher_set_key(enc, p2, keySize);
        botan_cipher_start(enc, nullptr, 0);
        botan_cipher_update(enc, BOTAN_CIPHER_UPDATE_FLAG_FINAL, p1, halfblockSize, &written, p2, halfblockSize, &consumed);
        // ------------------------------------
        
        while(Spmc_Queue_Size >= SPMC_MAX_QUEUE_SIZE)
            std::this_thread::sleep_for(std::chrono::milliseconds(SPMC_QUEUE_POLLING_INTERVAL_MS)); // wait for some items to be deallocated
        rootNode = new Spmc_Queue_Node(v);
        *curNext = rootNode;
        curNext = &(rootNode->nextNode);
        Spmc_Queue_Size++; // fetch_add
        remainingToGenerate -= SPMC_QUEUE_BSIZE;
    }
	*curNext = nullptr; // signal consumers that there is not next block so they can finish their tasks and return

    // join consumer threads
    writerThread.join();
    if(algo != nullptr) hasherThread->join();
    fd.close(); // TODO file had better be closed from writer consumer
    // PRINTUNIFIED("Queue size at the end was %d\n",Spmc_Queue_Size.load());
    // Spmc_Queue_Size = 0;

    // collect hash
    if(algo != nullptr) {
        auto result = consumer.hash1->final();
        output_hash = Botan::hex_encode(result);
    }
    else output_hash = ""; // reset, contains hash label in input, but it's unused here, since we use *algo
    return 0;
}

#endif /* __SPMC_LOCKFREE_QUEUE__ */