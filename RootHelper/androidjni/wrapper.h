#ifdef ANDROID_NDK
#ifndef _RH_ANDROIDJNI_WRAPPER_H_
#define _RH_ANDROIDJNI_WRAPPER_H_

#include <jni.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "botan_all.h"

// DUPLICATED CODE, possibly move in common header
#include <android/log.h>

// used by hashCode of AuthData and by HashView
// returns a 32-bit hash to be used in custom hashCodes for Java beans
uint32_t hashCodeDefault(const uint8_t* input, uint32_t size) {
	// if needed, replace with crc32
	uint8_t result[28]=""; // 224/8 = 28
	botan_hash_t hash1{};
	
    botan_hash_init(&hash1,"SHA-224",0);
	botan_hash_update(hash1,input,size);
	botan_hash_final(hash1,result);
	
	botan_hash_destroy(hash1);
	// take 32-bit MSB of SHA-224 as integer
	uint32_t* r = (uint32_t*)result;
	return *r;
}

extern "C" {
JNIEXPORT jstring JNICALL
Java_it_pgp_Native_stringFromJNI(JNIEnv *env, jclass type) {
    std::string hello = "Test string for RH's JNI wrapper";
    return env->NewStringUTF(hello.c_str());
}

JNIEXPORT jint JNICALL
Java_it_pgp_Native_isSymLink(JNIEnv *env, jclass type, jstring path_) {
	int ret;
	struct stat st = {};
	const char* path = (const char*)(env->GetStringUTFChars(path_,NULL));
	size_t length = (size_t)(env->GetStringLength(path_));
	lstat(path,&st);
	ret = S_ISLNK(st.st_mode)?1:0;
	env->ReleaseStringUTFChars(path_, path);
	return ret;
}

JNIEXPORT jint JNICALL
Java_it_pgp_Native_nHashCode(JNIEnv *env, jclass type, jbyteArray input_) {
	jbyte *input = env->GetByteArrayElements(input_, NULL);
	jsize l = env->GetArrayLength(input_);
	jint h = hashCodeDefault((uint8_t*)input,l);
	env->ReleaseByteArrayElements(input_, input, 0);
	return h;
}

// allocates byte array natively and returns it, will be freed by Java GC
JNIEXPORT jbyteArray JNICALL Java_it_pgp_Native_spongeForHashViewShake(JNIEnv *env, jclass type, jbyteArray input_, jsize inputLen, jsize outputLen) {
	uint8_t* input = (uint8_t*) env->GetPrimitiveArrayCritical(input_, 0);
	
	std::unique_ptr<Botan::HashFunction> sponge(new Botan::SHAKE_128(outputLen*8)); // ctor accepts bits, multiply by 8
	sponge->update(input,inputLen);
	Botan::secure_vector<uint8_t> output = sponge->final();
	env->ReleasePrimitiveArrayCritical(input_,input,0);
	
	jbyteArray data = env->NewByteArray(outputLen);
	env->SetByteArrayRegion(data, 0, outputLen, (jbyte*)(&output[0]));
	return data;
}

//~ JNIEXPORT void JNICALL
//~ Java_it_pgp_Native_c20StreamGen(JNIEnv *env, jclass type, jbyteArray key_, jbyteArray output_) {
	//~ jbyte *key = env->GetByteArrayElements(key_, NULL);
	//~ jsize keyLen = env->GetArrayLength(key_);
	//~ jbyte *output = env->GetByteArrayElements(output_, NULL);
	//~ jsize outputLen = env->GetArrayLength(output_);
	
	//~ uint8_t nonce[8] = {};
	//~ // to be provided already allocated by caller
	//~ std::vector<uint8_t> input(outputLen,0);
	
	//~ // using Botan C89 API, avoid messing up with std vectors & JNI layer 
	//~ botan_cipher_t enc = {};
	//~ botan_cipher_init(&enc, "ChaCha", Botan::ENCRYPTION);
    //~ botan_cipher_set_key(enc, (uint8_t*)key, (size_t)keyLen);
    //~ botan_cipher_start(enc, nonce, 8);
    //~ size_t written=0,consumed=0;
    //~ int rc = botan_cipher_update(enc, BOTAN_CIPHER_UPDATE_FLAG_FINAL, (uint8_t*)output, (size_t)outputLen, &written, &input[0], (size_t)outputLen, &consumed);
	
	//~ const char* keyhash = Botan::hex_encode(std::vector<uint8_t>(output,output+outputLen)).c_str();
	//~ __android_log_print(ANDROID_LOG_DEBUG, "RHJNIWrapper", "%s",keyhash);
	
	//~ botan_cipher_destroy(enc);
	
	//~ env->ReleaseByteArrayElements(output_, output, 0);
	//~ env->ReleaseByteArrayElements(key_, key, 0);
//~ }

//~ // arbitrary input, arbitrary output lengths
//~ JNIEXPORT void JNICALL
//~ Java_it_pgp_Native_spongeForHashView(JNIEnv *env, jclass type, jbyteArray input_, jbyteArray output_) {
	//~ jbyte *input = env->GetByteArrayElements(input_, NULL);
	//~ jsize inputLen = env->GetArrayLength(input_);
	//~ jbyte *output = env->GetByteArrayElements(output_, NULL);
	//~ jsize outputLen = env->GetArrayLength(output_);
	
	//~ uint8_t nonce[8] = {};
	//~ // to be provided already allocated by caller
	//~ std::vector<uint8_t> emptyInput(outputLen,0);
	
	//~ // using Botan C89 API, avoid messing up with std vectors & JNI layer 
	
	//~ // compression
	//~ std::unique_ptr<Botan::HashFunction> hash1(Botan::HashFunction::create("Blake2b"));
	//~ hash1->update((const uint8_t*)input,inputLen);
	//~ // std::vector<uint8_t> intermediate = hash1->final();
	//~ auto intermediate = hash1->final();
	
	//~ // expansion
	//~ botan_cipher_t enc = {};
	//~ botan_cipher_init(&enc, "ChaCha", Botan::ENCRYPTION);
    //~ botan_cipher_set_key(enc, &intermediate[0], intermediate.size());
    //~ botan_cipher_start(enc, nonce, 8);
    //~ size_t written=0,consumed=0;
    //~ int rc = botan_cipher_update(enc, BOTAN_CIPHER_UPDATE_FLAG_FINAL, (uint8_t*)output, (size_t)outputLen, &written, &emptyInput[0], (size_t)outputLen, &consumed);
	
	//~ botan_cipher_destroy(enc);
	
	//~ env->ReleaseByteArrayElements(output_, output, 0);
	//~ env->ReleaseByteArrayElements(input_, input, 0);
//~ }

//~ // this works, but chacha is not done in-place
//~ JNIEXPORT void JNICALL
//~ Java_it_pgp_Native_spongeForHashViewInPlace(JNIEnv *env, jclass type, jbyteArray input_, jsize inputLen, jbyteArray output_, jsize outputLen) {	
	//~ uint8_t *input = (uint8_t *) env->GetPrimitiveArrayCritical(input_, 0);
	//~ uint8_t *output = (uint8_t *) env->GetPrimitiveArrayCritical(output_, 0);
	
	//~ std::vector<uint8_t> nonce(8,0);
	//~ // to be provided already allocated by caller
	//~ std::vector<uint8_t> emptyInput(outputLen,0);
	
	//~ // using Botan C89 API, avoid messing up with std vectors & JNI layer 
	
	//~ // compression
	//~ std::unique_ptr<Botan::HashFunction> hash1(Botan::HashFunction::create("SHA-256"));
	//~ hash1->update((const uint8_t*)input,inputLen);
	//~ Botan::secure_vector<uint8_t> intermediate = hash1->final();
	
	//~ // expansion
	//~ botan_cipher_t enc{};
	//~ botan_cipher_init(&enc, "ChaCha", Botan::ENCRYPTION);
    //~ botan_cipher_set_key(enc, &intermediate[0], intermediate.size());
    //~ botan_cipher_start(enc, &nonce[0], nonce.size());
    //~ size_t written=0,consumed=0;
    //~ int rc = botan_cipher_update(enc, BOTAN_CIPHER_UPDATE_FLAG_FINAL, (uint8_t*)output, (size_t)outputLen, &written, &emptyInput[0], (size_t)outputLen, &consumed);
	
	//~ botan_cipher_destroy(enc);
	
	//~ env->ReleasePrimitiveArrayCritical(input_,input,0);
	//~ env->ReleasePrimitiveArrayCritical(output_,output,0);
//~ }

}

#endif /* _RH_ANDROIDJNI_WRAPPER_H_ */
#else
#endif
