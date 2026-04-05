#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Header-only PCA dimensionality reduction for spectral features.
// Uses JAMA eigenvalue decomposition (bundled with Essentia).
//
// RT path:  circular buffer -> covariance -> eigendecomp -> project
// Batch path: static computeBatchPCA() over entire feature matrix

#include <essentia/utils/tnt/tnt_array1d.h>
#include <essentia/utils/tnt/tnt_array2d.h>
#include <essentia/utils/tnt/jama_eig.h>

#include <vector>
#include <cmath>
#include <algorithm>

namespace EssentiaTD
{

class PCAProcessor
{
public:
	/// Configure PCA dimensions. Resets window if parameters change.
	void configure(int numDimensions, int numComponents, int windowSize)
	{
		numComponents = std::min(numComponents, numDimensions);

		if (numDimensions == myNumDimensions &&
		    numComponents == myNumComponents &&
		    windowSize    == myWindowSize)
			return;

		myNumDimensions = numDimensions;
		myNumComponents = numComponents;
		myWindowSize    = windowSize;

		myWindow.assign(static_cast<size_t>(windowSize),
		                std::vector<float>(static_cast<size_t>(numDimensions), 0.0f));
		myWritePos   = 0;
		myFrameCount = 0;

		myMean.assign(static_cast<size_t>(numDimensions), 0.0f);
		myEigenvectors.assign(static_cast<size_t>(numComponents),
		                      std::vector<float>(static_cast<size_t>(numDimensions), 0.0f));
		myEigenvalues.assign(static_cast<size_t>(numComponents), 0.0f);
		myVarianceRatios.assign(static_cast<size_t>(numComponents), 0.0f);
		myPrevEigenvectors.clear();
		myReady = false;
	}

	/// Push one frame of features into the circular buffer.
	void pushFrame(const float* features, int numFeatures)
	{
		if (numFeatures != myNumDimensions || myWindowSize <= 0)
			return;

		auto& slot = myWindow[static_cast<size_t>(myWritePos)];
		for (int i = 0; i < numFeatures; ++i)
			slot[static_cast<size_t>(i)] = features[i];

		myWritePos = (myWritePos + 1) % myWindowSize;
		if (myFrameCount < myWindowSize)
			++myFrameCount;
	}

	/// Recompute eigenvectors from the current window contents.
	void recompute()
	{
		if (myFrameCount < 2 || myNumDimensions < 1)
		{
			myReady = false;
			return;
		}

		const int N = myFrameCount;
		const int D = myNumDimensions;

		// 1. Mean
		myMean.assign(static_cast<size_t>(D), 0.0f);
		for (int i = 0; i < N; ++i)
		{
			const auto& frame = myWindow[static_cast<size_t>(i)];
			for (int d = 0; d < D; ++d)
				myMean[static_cast<size_t>(d)] += frame[static_cast<size_t>(d)];
		}
		const float invN = 1.0f / static_cast<float>(N);
		for (int d = 0; d < D; ++d)
			myMean[static_cast<size_t>(d)] *= invN;

		// 2. Covariance (upper triangle, then mirror)
		TNT::Array2D<double> cov(D, D, 0.0);
		for (int i = 0; i < N; ++i)
		{
			const auto& frame = myWindow[static_cast<size_t>(i)];
			for (int a = 0; a < D; ++a)
			{
				const double da = static_cast<double>(
					frame[static_cast<size_t>(a)] - myMean[static_cast<size_t>(a)]);
				for (int b = a; b < D; ++b)
				{
					const double db = static_cast<double>(
						frame[static_cast<size_t>(b)] - myMean[static_cast<size_t>(b)]);
					cov[a][b] += da * db;
				}
			}
		}
		const double invN1 = 1.0 / static_cast<double>(N - 1);
		for (int a = 0; a < D; ++a)
		{
			for (int b = a; b < D; ++b)
			{
				cov[a][b] *= invN1;
				cov[b][a] = cov[a][b];
			}
		}

		// 3. Diagonal regularization
		for (int d = 0; d < D; ++d)
			cov[d][d] += 1e-10;

		// 4. Eigendecomposition
		JAMA::Eigenvalue<double> eig(cov);
		TNT::Array2D<double> V;
		TNT::Array1D<double> eigenvalues;
		eig.getV(V);
		eig.getRealEigenvalues(eigenvalues);

		// 5. Top-N eigenvectors (JAMA: ascending order -> read from end)
		double totalVariance = 0.0;
		for (int i = 0; i < D; ++i)
			totalVariance += std::max(eigenvalues[i], 0.0);

		const int nc = myNumComponents;
		for (int c = 0; c < nc; ++c)
		{
			const int idx = D - 1 - c;
			myEigenvalues[static_cast<size_t>(c)] =
				static_cast<float>(std::max(eigenvalues[idx], 0.0));

			auto& ev = myEigenvectors[static_cast<size_t>(c)];
			for (int d = 0; d < D; ++d)
				ev[static_cast<size_t>(d)] = static_cast<float>(V[d][idx]);

			myVarianceRatios[static_cast<size_t>(c)] = (totalVariance > 0.0)
				? static_cast<float>(std::max(eigenvalues[idx], 0.0) / totalVariance)
				: 0.0f;
		}

		// 6. Sign-flip correction for temporal coherence
		if (!myPrevEigenvectors.empty() &&
		    static_cast<int>(myPrevEigenvectors.size()) == nc)
		{
			for (int c = 0; c < nc; ++c)
			{
				float dot = 0.0f;
				const auto& cur  = myEigenvectors[static_cast<size_t>(c)];
				const auto& prev = myPrevEigenvectors[static_cast<size_t>(c)];
				for (int d = 0; d < D; ++d)
					dot += cur[static_cast<size_t>(d)] * prev[static_cast<size_t>(d)];
				if (dot < 0.0f)
				{
					auto& ev = myEigenvectors[static_cast<size_t>(c)];
					for (int d = 0; d < D; ++d)
						ev[static_cast<size_t>(d)] = -ev[static_cast<size_t>(d)];
				}
			}
		}
		myPrevEigenvectors = myEigenvectors;

		myReady = true;
	}

	/// Project a feature vector onto the principal components.
	void project(const float* features, float* output) const
	{
		if (!myReady)
		{
			for (int c = 0; c < myNumComponents; ++c)
				output[c] = 0.0f;
			return;
		}

		for (int c = 0; c < myNumComponents; ++c)
		{
			float val = 0.0f;
			const auto& ev = myEigenvectors[static_cast<size_t>(c)];
			for (int d = 0; d < myNumDimensions; ++d)
				val += ev[static_cast<size_t>(d)]
				     * (features[d] - myMean[static_cast<size_t>(d)]);
			output[c] = val;
		}
	}

	float varianceRatio(int component) const
	{
		if (component < 0 || component >= static_cast<int>(myVarianceRatios.size()))
			return 0.0f;
		return myVarianceRatios[static_cast<size_t>(component)];
	}

	bool isReady()    const { return myReady; }
	int  dimensions() const { return myNumDimensions; }
	int  components() const { return myNumComponents; }

	// ----- Batch PCA (static, no instance state) -----

	/// Compute PCA over an entire feature matrix.
	/// featureCache layout: [channel][frame], channels 0..numSpectralCh-1
	static void computeBatchPCA(
		const std::vector<std::vector<float>>& featureCache,
		int numSpectralCh,
		int numFrames,
		int numComponents,
		std::vector<std::vector<float>>& pcCache,
		std::vector<float>& varianceRatios)
	{
		const int D  = numSpectralCh;
		const int N  = numFrames;
		const int nc = std::min(numComponents, D);

		pcCache.assign(static_cast<size_t>(nc),
		               std::vector<float>(static_cast<size_t>(N), 0.0f));
		varianceRatios.assign(static_cast<size_t>(nc), 0.0f);

		if (N < 2 || D < 1) return;

		// Mean per dimension
		std::vector<double> mean(static_cast<size_t>(D), 0.0);
		for (int d = 0; d < D; ++d)
		{
			const auto& ch = featureCache[static_cast<size_t>(d)];
			for (int f = 0; f < N; ++f)
				mean[static_cast<size_t>(d)] += static_cast<double>(ch[static_cast<size_t>(f)]);
			mean[static_cast<size_t>(d)] /= N;
		}

		// Covariance
		TNT::Array2D<double> cov(D, D, 0.0);
		for (int f = 0; f < N; ++f)
		{
			for (int a = 0; a < D; ++a)
			{
				const double da = static_cast<double>(
					featureCache[static_cast<size_t>(a)][static_cast<size_t>(f)])
					- mean[static_cast<size_t>(a)];
				for (int b = a; b < D; ++b)
				{
					const double db = static_cast<double>(
						featureCache[static_cast<size_t>(b)][static_cast<size_t>(f)])
						- mean[static_cast<size_t>(b)];
					cov[a][b] += da * db;
				}
			}
		}
		const double invN1 = 1.0 / static_cast<double>(N - 1);
		for (int a = 0; a < D; ++a)
		{
			for (int b = a; b < D; ++b)
			{
				cov[a][b] *= invN1;
				cov[b][a] = cov[a][b];
			}
		}
		for (int d = 0; d < D; ++d)
			cov[d][d] += 1e-10;

		// Eigendecomposition
		JAMA::Eigenvalue<double> eig(cov);
		TNT::Array2D<double> V;
		TNT::Array1D<double> eigenvalues;
		eig.getV(V);
		eig.getRealEigenvalues(eigenvalues);

		double totalVariance = 0.0;
		for (int i = 0; i < D; ++i)
			totalVariance += std::max(eigenvalues[i], 0.0);

		// Extract top-N and project all frames
		for (int c = 0; c < nc; ++c)
		{
			const int idx = D - 1 - c;
			varianceRatios[static_cast<size_t>(c)] = (totalVariance > 0.0)
				? static_cast<float>(std::max(eigenvalues[idx], 0.0) / totalVariance)
				: 0.0f;

			auto& pcCh = pcCache[static_cast<size_t>(c)];
			for (int f = 0; f < N; ++f)
			{
				double val = 0.0;
				for (int d = 0; d < D; ++d)
					val += V[d][idx] * (static_cast<double>(
						featureCache[static_cast<size_t>(d)][static_cast<size_t>(f)])
						- mean[static_cast<size_t>(d)]);
				pcCh[static_cast<size_t>(f)] = static_cast<float>(val);
			}
		}
	}

private:
	int myNumDimensions = 0;
	int myNumComponents = 3;
	int myWindowSize    = 512;

	std::vector<std::vector<float>> myWindow;
	int myWritePos   = 0;
	int myFrameCount = 0;

	std::vector<float> myMean;
	std::vector<std::vector<float>> myEigenvectors;  // [component][dimension]
	std::vector<float> myEigenvalues;
	std::vector<float> myVarianceRatios;
	bool myReady = false;

	std::vector<std::vector<float>> myPrevEigenvectors;
};

} // namespace EssentiaTD
