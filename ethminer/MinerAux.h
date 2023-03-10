#pragma once

/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file MinerAux.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * CLI module for mining.
 */

#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <random>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim_all.hpp>
#include <boost/optional.hpp>

#include <libethcore/Exceptions.h>
#include <libdevcore/SHA3.h>
#include <libethcore/EthashAux.h>
#include <libethcore/Farm.h>
#include <ethminer-buildinfo.h>
#if ETH_ETHASHCL
#include <libethash-cl/CLMiner.h>
#endif
#if ETH_ETHASHCUDA
#include <libethash-cuda/CUDAMiner.h>
#endif
#include <libpoolprotocols/PoolManager.h>
#include <libpoolprotocols/stratum/EthStratumClient.h>
#include <libpoolprotocols/getwork/EthGetworkClient.h>
#include <libpoolprotocols/testing/SimulateClient.h>

#if ETH_DBUS
#include "DBusInt.h"
#endif
#if API_CORE
#include <libapicore/Api.h>
#endif

using namespace std;
using namespace dev;
using namespace dev::eth;


class BadArgument: public Exception {};
struct MiningChannel: public LogChannel
{
	static const char* name() { return EthGreen "  m"; }
	static const int verbosity = 2;
	static const bool debug = false;
};
#define minelog clog(MiningChannel)

inline std::string toJS(unsigned long _n)
{
	std::string h = toHex(toCompactBigEndian(_n, 1));
	// remove first 0, if it is necessary;
	std::string res = h[0] != '0' ? h : h.substr(1);
	return "0x" + res;
}

bool g_running = false;

class MinerCLI
{
public:

	MinerCLI() {}

	static void signalHandler(int sig)
	{
		(void)sig;
		g_running = false;
	}

	void deprecated(const string& arg)
	{
		cerr << "Warning: " << arg << " is deprecated. Use the -P parameter instead." << endl;
	}

	bool interpretOption(int& i, int argc, char** argv)
	{
		string arg = argv[i];

		if ((arg == "--exit"))
		{
			m_exit = true;
		}
#if ETH_ETHASHCL
		else if (arg == "--opencl-platform" && i + 1 < argc)
			try {
				m_openclPlatform = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		else if (arg == "--opencl-devices" || arg == "--opencl-device")
			while (m_openclDeviceCount < MAX_MINERS && i + 1 < argc)
			{
				try
				{
					m_openclDevices[m_openclDeviceCount] = stol(argv[++i]);
					++m_openclDeviceCount;
				}
				catch (...)
				{
					i--;
					break;
				}
			}
		else if(arg == "--cl-parallel-hash" && i + 1 < argc) {
			try {
				m_openclThreadsPerHash = stol(argv[++i]);
				if(m_openclThreadsPerHash != 1 && m_openclThreadsPerHash != 2 &&
				   m_openclThreadsPerHash != 4 && m_openclThreadsPerHash != 8) {
					BOOST_THROW_EXCEPTION(BadArgument());
				}
			}
			catch(...) {
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
		else if (arg == "--cl-kernel" && i + 1 < argc)
		{
			try
			{
				m_openclSelectedKernel = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
		else if ( arg == "--cl-global-work"  && i + 1 < argc)
		{
			try
			{
				m_globalWorkSizeMultiplier = stol(argv[++i]);

			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
		else if ( arg == "--cl-local-work" && i + 1 < argc)
			try
			{
					m_localWorkSize = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
#endif
#if ETH_ETHASHCL || ETH_ETHASHCUDA
		else if (arg == "--list-devices")
			m_shouldListDevices = true;
#endif
#if ETH_ETHASHCUDA
		else if ( arg == "--cuda-grid-size" && i + 1 < argc)
			try
			{
				m_cudaGridSize = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		else if ( arg == "--cuda-block-size" && i + 1 < argc)
			try
			{
				m_cudaBlockSize= stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		else if (arg == "--cuda-devices")
		{
			while (m_cudaDeviceCount < MAX_MINERS && i + 1 < argc)
			{
				try
				{
					m_cudaDevices[m_cudaDeviceCount] = stol(argv[++i]);
					++m_cudaDeviceCount;
				}
				catch (...)
				{
					i--;
					break;
				}
			}
		}
                else if (arg == "--cuda-parallel-hash" && i + 1 < argc)
                {
                        try {
                                m_parallelHash = stol(argv[++i]);
                                if (m_parallelHash == 0 || m_parallelHash > 8)
                                {
                                    throw BadArgument();
                                }
                        }
                        catch (...)
                        {
                                cerr << "Bad " << arg << " option: " << argv[i] << endl;
                                BOOST_THROW_EXCEPTION(BadArgument());
                        }
                }
		else if (arg == "--cuda-schedule" && i + 1 < argc)
		{
			string mode = argv[++i];
			if (mode == "auto") m_cudaSchedule = 0;
			else if (mode == "spin") m_cudaSchedule = 1;
			else if (mode == "yield") m_cudaSchedule = 2;
			else if (mode == "sync") m_cudaSchedule = 4;
			else
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
		else if (arg == "--cuda-streams" && i + 1 < argc)
			m_numStreams = stol(argv[++i]);
		else if (arg == "--cuda-noeval")
			m_cudaNoEval = true;
#endif
		else if ((arg == "-L" || arg == "--dag-load-mode") && i + 1 < argc)
		{
			string mode = argv[++i];
			if (mode == "parallel") m_dagLoadMode = DAG_LOAD_MODE_PARALLEL;
			else if (mode == "sequential") m_dagLoadMode = DAG_LOAD_MODE_SEQUENTIAL;
			else if (mode == "single")
			{
				m_dagLoadMode = DAG_LOAD_MODE_SINGLE;
				m_dagCreateDevice = stol(argv[++i]);
			}
			else
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
		else if (arg == "--benchmark-warmup" && i + 1 < argc)
			try {
				m_benchmarkWarmup = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		else if (arg == "--benchmark-trial" && i + 1 < argc)
			try {
				m_benchmarkTrial = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		else if (arg == "--benchmark-trials" && i + 1 < argc)
			try
			{
				m_benchmarkTrials = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		else if (arg == "-G" || arg == "--opencl")
			m_minerType = MinerType::CL;
		else if (arg == "-U" || arg == "--cuda")
		{
			m_minerType = MinerType::CUDA;
		}
		else if (arg == "-X" || arg == "--cuda-opencl")
		{
			m_minerType = MinerType::Mixed;
		}
		else if ((arg == "-t" || arg == "--mining-threads") && i + 1 < argc)
		{
			try
			{
				m_miningThreads = stol(argv[++i]);
			}
			catch (...)
			{
				cerr << "Bad " << arg << " option: " << argv[i] << endl;
				BOOST_THROW_EXCEPTION(BadArgument());
			}
		}
		else
			return false;
		return true;
	}

	void execute()
	{
		if (m_shouldListDevices)
		{
#if ETH_ETHASHCL
			if (m_minerType == MinerType::CL || m_minerType == MinerType::Mixed)
				CLMiner::listDevices();
#endif
#if ETH_ETHASHCUDA
			if (m_minerType == MinerType::CUDA || m_minerType == MinerType::Mixed)
				CUDAMiner::listDevices();
#endif
			exit(0);
		}

		auto* build = ethminer_get_buildinfo();
		minelog << "ethminer version " << build->project_version;
		minelog << "Build: " << build->system_name << "/" << build->build_type
			 << "+git." << string(build->git_commit_hash).substr(0, 7);

		if (m_minerType == MinerType::CL || m_minerType == MinerType::Mixed)
		{
#if ETH_ETHASHCL
			if (m_openclDeviceCount > 0)
			{
				CLMiner::setDevices(m_openclDevices, m_openclDeviceCount);
				m_miningThreads = m_openclDeviceCount;
			}

			CLMiner::setCLKernel(m_openclSelectedKernel);
			CLMiner::setThreadsPerHash(m_openclThreadsPerHash);

			if (!CLMiner::configureGPU(
					m_localWorkSize,
					m_globalWorkSizeMultiplier,
					m_openclPlatform,
					0,
					m_dagLoadMode,
					m_dagCreateDevice,
					m_exit
				))
				exit(1);
			CLMiner::setNumInstances(m_miningThreads);
#else
			cerr << "Selected GPU mining without having compiled with -DETHASHCL=1" << endl;
			exit(1);
#endif
		}
		if (m_minerType == MinerType::CUDA || m_minerType == MinerType::Mixed)
		{
#if ETH_ETHASHCUDA
			if (m_cudaDeviceCount > 0)
			{
				CUDAMiner::setDevices(m_cudaDevices, m_cudaDeviceCount);
				m_miningThreads = m_cudaDeviceCount;
			}

			CUDAMiner::setNumInstances(m_miningThreads);
			if (!CUDAMiner::configureGPU(
				m_cudaBlockSize,
				m_cudaGridSize,
				m_numStreams,
				m_cudaSchedule,
				0,
				m_dagLoadMode,
				m_dagCreateDevice,
				m_cudaNoEval,
				m_exit
				))
				exit(1);

			CUDAMiner::setParallelHash(m_parallelHash);
#else
			cerr << "CUDA support disabled. Configure project build with -DETHASHCUDA=ON" << endl;
			exit(1);
#endif
		}

		g_running = true;
		signal(SIGINT, MinerCLI::signalHandler);
		signal(SIGTERM, MinerCLI::signalHandler);

//		if (m_mode == OperationMode::Benchmark)
//			doBenchmark(m_minerType, m_benchmarkWarmup, m_benchmarkTrial, m_benchmarkTrials);
//		else if (m_mode == OperationMode::Farm || m_mode == OperationMode::Stratum || m_mode == OperationMode::Simulation)
        doMiner(m_minerType);
	}

	static void streamHelp(ostream& _out)
	{
		_out
			<< "Work farming mode:" << endl
			<< "    -F,--farm <url>  (deprecated) Put into mining farm mode with the work server at URL (default: http://127.0.0.1:8545)" << endl
			<< "    -FF,-FO, --farm-failover, --stratum-failover <url> (deprecated) Failover getwork/stratum URL (default: disabled)" << endl
			<< "	--farm-retries <n> Number of retries until switch to failover (default: 3)" << endl
			<< "	-S, --stratum <host:port>  (deprecated) Put into stratum mode with the stratum server at host:port" << endl
			<< "	-SF, --stratum-failover <host:port>  (deprecated) Failover stratum server at host:port" << endl
			<< "    -O, --userpass <username.workername:password> (deprecated) Stratum login credentials" << endl
			<< "    -FO, --failover-userpass <username.workername:password> (deprecated) Failover stratum login credentials (optional, will use normal credentials when omitted)" << endl
			<< "    --work-timeout <n> reconnect/failover after n seconds of working on the same (stratum) job. Defaults to 180. Don't set lower than max. avg. block time" << endl
			<< "    --stratum-ssl [<n>]  (deprecated) Use encryption to connect to stratum server." << endl
			<< "        0: Force TLS1.2 (default)" << endl
			<< "        1: Allow any TLS version" << endl
			<< "        2: Allow self-signed or invalid certs and any TLS version" << endl
			<< "    -SP, --stratum-protocol <n> (deprecated) Choose which stratum protocol to use:" << endl
			<< "        0: official stratum spec: ethpool, ethermine, coinotron, mph, nanopool (default)" << endl
			<< "        1: eth-proxy compatible: dwarfpool, f2pool, nanopool (required for hashrate reporting to work with nanopool)" << endl
			<< "        2: EthereumStratum/1.0.0: nicehash" << endl
			<< "    -RH, --report-hashrate Report current hashrate to pool (please only enable on pools supporting this)" << endl
			<< "    -HWMON [<n>], Displays gpu temp, fan percent and power usage. Note: In linux, the program uses sysfs, which may require running with root privileges." << endl
			<< "        0: Displays only temp and fan percent (default)" << endl
			<< "        1: Also displays power usage" << endl
			<< "    --exit Stops the miner whenever an error is encountered" << endl
			<< "    -SE, --stratum-email <s> Email address used in eth-proxy (optional)" << endl
			<< "    --farm-recheck <n>  Leave n ms between checks for changed work (default: 500). When using stratum, use a high value (i.e. 2000) to get more stable hashrate output" << endl
			<< "    -P URL Specify a pool URL. Can be used multiple times. The 1st for for the primary pool, and the 2nd for the failover pool." << endl
			<< "        URL takes the form: scheme://user[:password]@hostname:port." << endl
			<< "        for getwork use one of the following schemes:" << endl
			<< "          " << URI::KnownSchemes(ProtocolFamily::GETWORK) << endl
			<< "        for stratum use one of the following schemes: "<< endl
			<< "          " << URI::KnownSchemes(ProtocolFamily::STRATUM) << endl
			<< "        Example: stratum+ssl://0x012345678901234567890234567890123.miner1@ethermine.org:5555" << endl
			<< endl
			<< "Benchmarking mode:" << endl
			<< "    -M [<n>],--benchmark [<n>] Benchmark for mining and exit; Optionally specify block number to benchmark against specific DAG." << endl
			<< "    --benchmark-warmup <seconds>  Set the duration of warmup for the benchmark tests (default: 3)." << endl
			<< "    --benchmark-trial <seconds>  Set the duration for each trial for the benchmark tests (default: 3)." << endl
			<< "    --benchmark-trials <n>  Set the number of benchmark trials to run (default: 5)." << endl
			<< "Simulation mode:" << endl
			<< "    -Z [<n>],--simulation [<n>] Mining test mode. Used to validate kernel optimizations. Optionally specify block number." << endl
			<< "Mining configuration:" << endl
			<< "    -G,--opencl  When mining use the GPU via OpenCL." << endl
			<< "    -U,--cuda  When mining use the GPU via CUDA." << endl
			<< "    -X,--cuda-opencl Use OpenCL + CUDA in a system with mixed AMD/Nvidia cards. May require setting --opencl-platform 1 or 2. Use --list-devices option to check which platform is your AMD. " << endl
			<< "    --opencl-platform <n>  When mining using -G/--opencl use OpenCL platform n (default: 0)." << endl
			<< "    --opencl-device <n>  When mining using -G/--opencl use OpenCL device n (default: 0)." << endl
			<< "    --opencl-devices <0 1 ..n> Select which OpenCL devices to mine on. Default is to use all" << endl
			<< "    -t, --mining-threads <n> Limit number of CPU/GPU miners to n (default: use everything available on selected platform)" << endl
			<< "    --list-devices List the detected OpenCL/CUDA devices and exit. Should be combined with -G, -U, or -X flag" << endl
			<< "    --display-interval <n> Set mining stats display interval in seconds. (default: every 5 seconds)" << endl			
			<< "    -L, --dag-load-mode <mode> DAG generation mode." << endl
			<< "        parallel    - load DAG on all GPUs at the same time (default)" << endl
			<< "        sequential  - load DAG on GPUs one after another. Use this when the miner crashes during DAG generation" << endl
			<< "        single <n>  - generate DAG on device n, then copy to other devices" << endl
#if ETH_ETHASHCL
			<< " OpenCL configuration:" << endl
			<< "    --cl-kernel <n>  Use a different OpenCL kernel (default: use stable kernel)" << endl
			<< "        0: stable kernel" << endl
			<< "        1: experimental kernel" << endl
			<< "    --cl-local-work Set the OpenCL local work size. Default is " << CLMiner::c_defaultLocalWorkSize << endl
			<< "    --cl-global-work Set the OpenCL global work size as a multiple of the local work size. Default is " << CLMiner::c_defaultGlobalWorkSizeMultiplier << " * " << CLMiner::c_defaultLocalWorkSize << endl
			<< "    --cl-parallel-hash <1 2 ..8> Define how many threads to associate per hash. Default=8" << endl
#endif
#if ETH_ETHASHCUDA
			<< " CUDA configuration:" << endl
			<< "    --cuda-block-size Set the CUDA block work size. Default is " << toString(CUDAMiner::c_defaultBlockSize) << endl
			<< "    --cuda-grid-size Set the CUDA grid size. Default is " << toString(CUDAMiner::c_defaultGridSize) << endl
			<< "    --cuda-streams Set the number of CUDA streams. Default is " << toString(CUDAMiner::c_defaultNumStreams) << endl
			<< "    --cuda-schedule <mode> Set the schedule mode for CUDA threads waiting for CUDA devices to finish work. Default is 'sync'. Possible values are:" << endl
			<< "        auto  - Uses a heuristic based on the number of active CUDA contexts in the process C and the number of logical processors in the system P. If C > P, then yield else spin." << endl
			<< "        spin  - Instruct CUDA to actively spin when waiting for results from the device." << endl
			<< "        yield - Instruct CUDA to yield its thread when waiting for results from the device." << endl
			<< "        sync  - Instruct CUDA to block the CPU thread on a synchronization primitive when waiting for the results from the device." << endl
			<< "    --cuda-devices <0 1 ..n> Select which CUDA GPUs to mine on. Default is to use all" << endl
			<< "    --cuda-parallel-hash <1 2 ..8> Define how many hashes to calculate in a kernel, can be scaled to achieve better performance. Default=4" << endl
			<< "    --cuda-noeval  bypass host software re-evaluation of GPU solutions." << endl
			<< "        This will trim some milliseconds off the time it takes to send a result to the pool." << endl
			<< "        Use at your own risk! If GPU generates errored results they WILL be forwarded to the pool" << endl
			<< "        Not recommended at high overclock." << endl
#endif
#if API_CORE
			<< " API core configuration:" << endl
			<< "    --api-port Set the api port, the miner should listen to. Use 0 to disable. Default=0, use negative numbers to run in readonly mode. for example -3333." << endl
#endif
			;
	}

private:

	void doBenchmark(MinerType _m, unsigned _warmupDuration = 15, unsigned _trialDuration = 3, unsigned _trials = 5)
	{
        /* BlockHeader genesis;
		genesis.setNumber(m_benchmarkBlock);
		genesis.setDifficulty(u256(1) << 64);

		string platformInfo = _m == MinerType::CL ? "CL" : "CUDA";
		cout << "Benchmarking on platform: " << platformInfo << endl;

		cout << "Preparing DAG for block #" << m_benchmarkBlock << endl;
		//genesis.prep();

		if (_m == MinerType::CL)
			f.start("opencl", false);
		else if (_m == MinerType::CUDA)
			f.start("cuda", false);

		WorkPackage current = WorkPackage(genesis);
		

		vector<uint64_t> results;
		results.reserve(_trials);
		uint64_t mean = 0;
		uint64_t innerMean = 0;
		for (unsigned i = 0; i <= _trials; ++i)
		{
			current.header = h256::random();
			current.boundary = genesis.boundary();
			f.setWork(current);	
			if (!i)
				cout << "Warming up..." << endl;
			else
				cout << "Trial " << i << "... " << flush <<endl;
			this_thread::sleep_for(chrono::seconds(i ? _trialDuration : _warmupDuration));

			auto mp = f.miningProgress();
			if (!i)
				continue;
			auto rate = mp.rate();

			cout << rate << endl;
			results.push_back(rate);
			mean += rate;
		}
		sort(results.begin(), results.end());
		cout << "min/mean/max: " << results.front() << "/" << (mean / _trials) << "/" << results.back() << " H/s" << endl;
		if (results.size() > 2) {
			for (auto it = results.begin()+1; it != results.end()-1; it++)
				innerMean += *it;
			innerMean /= (_trials - 2);
			cout << "inner mean: " << innerMean << " H/s" << endl;
		}
		else
			cout << "inner mean: n/a" << endl;

		exit(0);*/
	}
	
	void doMiner(MinerType _m)
	{

		BlockHeader genesis;
        genesis.setNumber(m_benchmarkBlock);
        genesis.setDifficulty(u256(1) << 64);
	
		Miner* miner;
        
		if (_m == MinerType::CL)
            miner = new CLMiner;
		else
			miner = new CUDAMiner;
        string platformInfo = _m == MinerType::CL ? "CL" : "CUDA";
        cout << "Benchmarking on platform: " << platformInfo << endl;

        cout << "Preparing DAG for block #" << m_benchmarkBlock << endl;
        // genesis.prep();


        WorkPackage current = WorkPackage(genesis);


        vector<uint64_t> results;
        int _trials = 3;
        results.reserve(_trials);
        uint64_t mean = 0;
	    current.header = h256::random();
        current.boundary = genesis.boundary();
        miner->setWork(current);
        miner->startWorking();
		exit(0);

	}


	/// Mining options
	MinerType m_minerType = MinerType::Mixed;
	unsigned m_openclPlatform = 0;
	unsigned m_miningThreads = UINT_MAX;
	bool m_shouldListDevices = false;
#if ETH_ETHASHCL
	unsigned m_openclSelectedKernel = 0;  ///< A numeric value for the selected OpenCL kernel
	unsigned m_openclDeviceCount = 0;
	vector<unsigned> m_openclDevices = vector<unsigned>(MAX_MINERS, -1);
	unsigned m_openclThreadsPerHash = 8;
	unsigned m_globalWorkSizeMultiplier = CLMiner::c_defaultGlobalWorkSizeMultiplier;
	unsigned m_localWorkSize = CLMiner::c_defaultLocalWorkSize;
#endif
#if ETH_ETHASHCUDA
	unsigned m_cudaDeviceCount = 0;
	vector<unsigned> m_cudaDevices = vector<unsigned>(MAX_MINERS, -1);
	unsigned m_numStreams = CUDAMiner::c_defaultNumStreams;
	unsigned m_cudaSchedule = 4; // sync
	unsigned m_cudaGridSize = CUDAMiner::c_defaultGridSize;
	unsigned m_cudaBlockSize = CUDAMiner::c_defaultBlockSize;
	bool m_cudaNoEval = false;
	unsigned m_parallelHash    = 4;
#endif
	unsigned m_dagLoadMode = 0; // parallel
	unsigned m_dagCreateDevice = 0;
	bool m_exit = false;
	/// Benchmarking params
	unsigned m_benchmarkWarmup = 15;
	unsigned m_benchmarkTrial = 3;
	unsigned m_benchmarkTrials = 5;
	unsigned m_benchmarkBlock = 0;


#if ETH_DBUS
	DBusInt dbusint;
#endif
};
