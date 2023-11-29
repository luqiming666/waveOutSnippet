// waveOutSnippet.cpp by Adam Sporka
// A demonstration of the traditional win32 waveOut sound API.
// This will play a 440 Hz sine wave on the default audio interface.

// Please make sure to link winmm.lib

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <conio.h>
#include <math.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>

#include <windows.h>

#pragma comment(lib, "winmm.lib")

// Test playing from a local file
#define DATA_SOURCE_FROM_FILE 1


// The audio will be processed in buffers this many samples long:
#define BUFLEN 512

// This is the sampling frequency in Hz:
#define SAMPLE_RATE 48000

// 4 buffers will be used in total
#define BUFFER_COUNT 4

// Audio device handle
HWAVEOUT hWaveOut;

// Wave headers, one for each buffer
WAVEHDR whdr[BUFFER_COUNT];

// Audio data for of the buffers
unsigned char* buffer[BUFFER_COUNT];

// To know which buffer to process next
int round_robin = 0;

// Global counter of generated samples
int64_t N = 0;

#pragma pack(push, 1)
struct WavHeader {
	char chunkId[4];
	uint32_t chunkSize;
	char format[4];
	char subchunk1Id[4];
	uint32_t subchunk1Size;
	uint16_t audioFormat;
	uint16_t numChannels;
	uint32_t sampleRate;
	uint32_t byteRate;
	uint16_t blockAlign;
	uint16_t bitsPerSample;
	char subchunk2Id[4];
	uint32_t subchunk2Size;
};
#pragma pack(pop)

////////////////////////////////////////////////////////////////
WavHeader header;
std::vector<int8_t> pcmData;
int64_t bytesUsed = 0; // bytes which have been played

int parseWaveFile(const char* srcFile)
{
	// Codes from ChatGPT 3.5 *_^
	// 打开.wav文件
	std::ifstream file(srcFile, std::ios::binary);
	if (!file.is_open()) {
		std::cerr << "Failed to open file." << std::endl;
		return 1;
	}

	std::cout << "Parsing the wave file: " << srcFile << std::endl;

	// 读取文件头
	//WavHeader header;
	file.read(reinterpret_cast<char*>(&header), sizeof(WavHeader));

	// 检查文件头是否有效
	if (std::string(header.chunkId, 4) != "RIFF" || std::string(header.format, 4) != "WAVE") {
		std::cerr << "Invalid WAV file format." << std::endl;
		return 1;
	}

	// 读取PCM数据
	//std::vector<int8_t> pcmData(header.subchunk2Size);
	pcmData = std::vector<int8_t>(header.subchunk2Size);
	file.read(reinterpret_cast<char*>(pcmData.data()), header.subchunk2Size);

	// 输出文件信息
	std::cout << "Channels: " << header.numChannels << std::endl;
	std::cout << "Sample Rate: " << header.sampleRate << " Hz" << std::endl;
	std::cout << "Bits per Sample: " << header.bitsPerSample << std::endl;
	std::cout << "PCM Data Size: " << pcmData.size() << " bytes" << std::endl;

	// 关闭文件
	file.close();

	return 0;
}

void fillBufferFromFile(int count_samples_per_channel, int8_t* buffer)
{
	int8_t* pSourceData = reinterpret_cast<int8_t*>(pcmData.data()) + bytesUsed;
	int64_t bytesLeft = pcmData.size() - bytesUsed;
	int64_t bytesToRead = count_samples_per_channel * header.numChannels * header.bitsPerSample / 8;

	if (bytesToRead <= bytesLeft) {
		memcpy(buffer, pSourceData, bytesToRead);
		bytesUsed += bytesToRead;
	}
	else {
		memcpy(buffer, pSourceData, bytesLeft);
		bytesUsed = 0;
	}
}

////////////////////////////////////////////////////////////////
void synthesizeBuffer(int count_samples_per_channel, int16_t* buffer)
{
	double A440 = 440.0; // 440 Hz
	double PI = 3.14151926535;

	for (int a = 0; a < count_samples_per_channel; a++)
	{
		// Calculate the next value of the sine wave sample.
		double value = 0.1 * sin(N * A440 * 2 * PI / (double)SAMPLE_RATE);

		// Convert to 16-bit value
		int16_t v16bit = static_cast<int16_t>(value * 32767);

		// Write the value to the buffer. The buffer has two channels.
		// Copy the same value to both.
		*buffer = v16bit;
		buffer++;
		*buffer = v16bit;
		buffer++;

		// Increment the global sample counter.
		N++;
	}
}

////////////////////////////////////////////////////////////////
void allocateAndClearBuffers()
{
	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		// Allocate & clear the buffer
#if DATA_SOURCE_FROM_FILE
		int cBufSize = BUFLEN * header.numChannels * header.bitsPerSample / 8;
#else
		int cBufSize = BUFLEN * 2 * 2;
#endif
		buffer[i] = new unsigned char[cBufSize];
		memset(buffer[i], 0, cBufSize);

		// Fill out the header
		whdr[i].lpData = (char*)buffer[i];
		whdr[i].dwBufferLength = cBufSize;
		whdr[i].dwBytesRecorded = 0;
		whdr[i].dwUser = 0;
		whdr[i].dwFlags = 0;
		whdr[i].dwLoops = 0;
	}
}

////////////////////////////////////////////////////////////////
void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	switch (uMsg) {

	case WOM_OPEN:
		// When the audio device is opened
		allocateAndClearBuffers();
		break;

	case WOM_DONE:
		// When the audio device needs more data
#if DATA_SOURCE_FROM_FILE
		fillBufferFromFile(BUFLEN, (int8_t*)(buffer[round_robin % BUFFER_COUNT]));
#else
		synthesizeBuffer(BUFLEN, (int16_t*)(buffer[round_robin % BUFFER_COUNT]));
#endif 
		waveOutWrite(hWaveOut, &whdr[round_robin % BUFFER_COUNT], sizeof(whdr[round_robin % BUFFER_COUNT]));
		round_robin++;
		break;

	case WOM_CLOSE:
		// No need to implement
		break;

	default:
		break;
	}
}

////////////////////////////////////////////////////////////////
bool openAudio()
{
	DWORD dummy;

	// Open the sound card
	WAVEFORMATEX waveformatex;
	waveformatex.wFormatTag = WAVE_FORMAT_PCM;
#if DATA_SOURCE_FROM_FILE
	waveformatex.nSamplesPerSec = header.sampleRate;
	waveformatex.nChannels = header.numChannels;
	waveformatex.wBitsPerSample = header.bitsPerSample;
#else
	waveformatex.nSamplesPerSec = SAMPLE_RATE;
	waveformatex.nChannels = 2; // Stereo
	waveformatex.wBitsPerSample = 16;
#endif
	waveformatex.nBlockAlign = waveformatex.nChannels * waveformatex.wBitsPerSample / 8;
	waveformatex.nAvgBytesPerSec = waveformatex.nSamplesPerSec * waveformatex.nBlockAlign;
	waveformatex.cbSize = 0;

	// error code 32 if opening 8-channel format on a normal PC w/ a stereo sound card
	auto result = waveOutOpen(&hWaveOut, WAVE_MAPPER, &waveformatex, (DWORD_PTR)waveOutProc, (DWORD_PTR)&dummy, CALLBACK_FUNCTION | WAVE_FORMAT_DIRECT);
	if (result != MMSYSERR_NOERROR)
	{
		std::cout << "waveOutOpen error: " << result << std::endl;
		return false;
	}

	// Pre-buffer with silence
	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		waveOutPrepareHeader(hWaveOut, &whdr[i], sizeof(whdr[i]));
		waveOutWrite(hWaveOut, &whdr[i], sizeof(whdr[i]));
	}

	return true;
}

////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
#if DATA_SOURCE_FROM_FILE
	if (argc < 2) {
		// Test some local files... "D:\\Media\\Ring07.wav"; //nanjing_8c.wav //"7.1.wav"
		parseWaveFile("D:\\Media\\Ring07.wav");
	}
	else {
		parseWaveFile(argv[1]);
	}
#endif

	bool success = openAudio();

	if (!success)
	{
		printf("Unable to open the audio interface. Terminating.\n");
		return 1;
	}

	printf("You should hear sound now. Press Escape to quit.");

	char key;
	do {
 		key = _getch();
	}
	while (key != 27);
	return 0;
}
