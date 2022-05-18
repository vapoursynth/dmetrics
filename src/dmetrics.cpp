//////////////////////////////////////////
// This file contains a simple filter
// skeleton you can use to get started.
// With no changes it simply passes
// frames through.

#include <VapourSynth4.h>
#include <VSHelper4.h>

#include <memory>
#include <string>
#include <algorithm>

static constexpr size_t BLKSIZE = 24;

struct DMetricsSettings {
	bool tff;
	bool chroma;
	int nt;
	int y0;
	int y1;
};

struct DMetricsData {
    VSNode *node;
    const VSVideoInfo *vi;
	std::string mmprop;
	std::string vmprop;
	DMetricsSettings settings = {};
};

struct DMetricsSet {
	int c;
	int p;
	int highest_sumc;
	int highest_sump;
};

static void calculateTelecideMetrics(const VSFrame *frameC, const VSFrame *frameP, const DMetricsSettings &settings, DMetricsSet &metrics, const VSAPI *vsapi) {
	const bool tff = settings.tff;
	const bool chroma = settings.chroma;
	const unsigned int nt = settings.nt;
	const int y0 = settings.y0;
	const int y1 = settings.y1;

	ptrdiff_t pitchc = vsapi->getStride(frameC, 0);
	ptrdiff_t pitchp = pitchc;
	ptrdiff_t pitchcUV = vsapi->getStride(frameC, 1);
	ptrdiff_t pitchpUV = pitchcUV;

	const uint8_t *fcrp = vsapi->getReadPtr(frameC, 0);
	const uint8_t *fcrpU = vsapi->getReadPtr(frameC, 1);
	const uint8_t *fcrpV = vsapi->getReadPtr(frameC, 2);

	const uint8_t *fprp = vsapi->getReadPtr(frameP, 0);
	const uint8_t *fprpU = vsapi->getReadPtr(frameP, 1);
	const uint8_t *fprpV = vsapi->getReadPtr(frameP, 2);

	int w = vsapi->getFrameWidth(frameC, 0);
	int h = vsapi->getFrameHeight(frameC, 0);
	int wover2 = w / 2;
	int hover2 = h / 2;
	int xblocks = (w + BLKSIZE - 1) / BLKSIZE;
	int yblocks = (h + BLKSIZE - 1) / BLKSIZE;

	int x, y, p, c, tmp1, tmp2;
	bool vc;
	const unsigned char *currbot0, *currbot2, *prevbot0, *prevbot2;
	const unsigned char *prevtop0, *prevtop2, *prevtop4, *currtop0, *currtop2, *currtop4;
	const unsigned char *a0, *a2, *b0, *b2, *b4;
	unsigned int diff, index;
	ptrdiff_t apitchtimes4, bpitchtimes4, cpitchtimes4;
	unsigned int highest_sumc, highest_sump;

	constexpr size_t T = 4;

	size_t numblocks = xblocks * yblocks;

	unsigned int *sumc = new unsigned int[numblocks]();
	unsigned int *sump = new unsigned int[numblocks]();

	/* Clear the block sums. */
	/*
	for (y = 0; y < yblocks; y++) {
		for (x = 0; x < xblocks; x++) {
			sump[y * xblocks + x] = 0;
			sumc[y * xblocks + x] = 0;
		}
	}
	*/

	/* Find the best field match. Subsample the frames for speed. */
	currbot0 = fcrp + pitchc;
	currbot2 = fcrp + 3 * pitchc;
	currtop0 = fcrp;
	currtop2 = fcrp + 2 * pitchc;
	cpitchtimes4 = 4 * pitchc;
	currtop4 = fcrp + 4 * pitchc;
	prevbot0 = fprp + pitchp;
	prevbot2 = fprp + 3 * pitchp;
	prevtop0 = fprp;
	prevtop2 = fprp + 2 * pitchp;
	prevtop4 = fprp + 4 * pitchp;
	if (tff) {
		apitchtimes4 = 4 * pitchp;
		a0 = prevbot0;
		a2 = prevbot2;
		bpitchtimes4 = 4 * pitchc;
		b0 = currtop0;
		b2 = currtop2;
		b4 = currtop4;
	} else {
		apitchtimes4 = 4 * pitchc;
		a0 = currbot0;
		a2 = currbot2;
		bpitchtimes4 = 4 * pitchp;
		b0 = prevtop0;
		b2 = prevtop2;
		b4 = prevtop4;
	}
	p = c = 0;

	// Calculate the field match and film/video metrics.
	constexpr size_t skip = 1;
	for (y = 0, index = 0; y < h - 4; y += 4) {
		/* Exclusion band. Good for ignoring subtitles. */
		if (y0 == y1 || y < y0 || y > y1) {
			for (x = 0; x < w;) {
				index = (y / BLKSIZE) * xblocks + x / BLKSIZE;

				// Test combination with current frame.
				tmp1 = ((long)currbot0[x] + (long)currbot2[x]);
				//				diff = abs((long)currtop0[x] - (tmp1 >> 1));
				diff = abs((((long)currtop0[x] + (long)currtop2[x] + (long)currtop4[x])) - (tmp1 >> 1) - tmp1);
				if (diff > nt) {
					c += diff;
				}

				tmp1 = currbot0[x] + T;
				tmp2 = currbot0[x] - T;
				vc = (tmp1 < currtop0[x] && tmp1 < currtop2[x]) ||
					(tmp2 > currtop0[x] && tmp2 > currtop2[x]);
				if (vc) {
					sumc[index]++;
				}

				// Test combination with previous frame.
				tmp1 = ((long)a0[x] + (long)a2[x]);
				diff = abs((((long)b0[x] + (long)b2[x] + (long)b4[x])) - (tmp1 >> 1) - tmp1);
				if (diff > nt) {
					p += diff;
				}

				tmp1 = a0[x] + T;
				tmp2 = a0[x] - T;
				vc = (tmp1 < b0[x] && tmp1 < b2[x]) ||
					(tmp2 > b0[x] && tmp2 > b2[x]);
				if (vc) {
					sump[index]++;
				}

				x += skip;
				if (!(x & 3)) x += 4;
			}
		}
		currbot0 += cpitchtimes4;
		currbot2 += cpitchtimes4;
		currtop0 += cpitchtimes4;
		currtop2 += cpitchtimes4;
		currtop4 += cpitchtimes4;
		a0 += apitchtimes4;
		a2 += apitchtimes4;
		b0 += bpitchtimes4;
		b2 += bpitchtimes4;
		b4 += bpitchtimes4;
	}

	if (chroma) {
		cpitchtimes4 = 4 * pitchcUV;

		for (int z = 0; z < 2; z++) {
			// Do the same for the U plane.
			if (z == 0) {
				currbot0 = fcrpU + pitchcUV;
				currbot2 = fcrpU + 3 * pitchcUV;
				currtop0 = fcrpU;
				currtop2 = fcrpU + 2 * pitchcUV;
				currtop4 = fcrpU + 4 * pitchcUV;
				prevbot0 = fprpU + pitchpUV;
				prevbot2 = fprpU + 3 * pitchpUV;
				prevtop0 = fprpU;
				prevtop2 = fprpU + 2 * pitchpUV;
				prevtop4 = fprpU + 4 * pitchpUV;
			} else {
				currbot0 = fcrpV + pitchcUV;
				currbot2 = fcrpV + 3 * pitchcUV;
				currtop0 = fcrpV;
				currtop2 = fcrpV + 2 * pitchcUV;
				currtop4 = fcrpV + 4 * pitchcUV;
				prevbot0 = fprpV + pitchpUV;
				prevbot2 = fprpV + 3 * pitchpUV;
				prevtop0 = fprpV;
				prevtop2 = fprpV + 2 * pitchpUV;
				prevtop4 = fprpV + 4 * pitchpUV;
			}
			if (tff) {
				apitchtimes4 = 4 * pitchpUV;
				a0 = prevbot0;
				a2 = prevbot2;
				bpitchtimes4 = 4 * pitchcUV;
				b0 = currtop0;
				b2 = currtop2;
				b4 = currtop4;
			} else {
				apitchtimes4 = 4 * pitchcUV;
				a0 = currbot0;
				a2 = currbot2;
				bpitchtimes4 = 4 * pitchpUV;
				b0 = prevtop0;
				b2 = prevtop2;
				b4 = prevtop4;
			}

			for (y = 0; y < hover2 - 4; y += 4) {
				/* Exclusion band. Good for ignoring subtitles. */
				if (y0 == y1 || y < y0 / 2 || y > y1 / 2) {
					for (x = 0; x < wover2;) {
						index = (y / BLKSIZE) * xblocks + x / BLKSIZE;
						// Test combination with current frame.
						tmp1 = ((long)currbot0[x] + (long)currbot2[x]);
						diff = abs((((long)currtop0[x] + (long)currtop2[x] + (long)currtop4[x])) - (tmp1 >> 1) - tmp1);
						if (diff > nt) {
							c += diff;
						}

						tmp1 = currbot0[x] + T;
						tmp2 = currbot0[x] - T;
						vc = (tmp1 < currtop0[x] && tmp1 < currtop2[x]) ||
							(tmp2 > currtop0[x] && tmp2 > currtop2[x]);
						if (vc) {
							sumc[index]++;
						}

						// Test combination with previous frame.
						tmp1 = ((long)a0[x] + (long)a2[x]);
						diff = abs((((long)b0[x] + (long)b2[x] + (long)b4[x])) - (tmp1 >> 1) - tmp1);
						if (diff > nt) {
							p += diff;
						}

						tmp1 = a0[x] + T;
						tmp2 = a0[x] - T;
						vc = (tmp1 < b0[x] && tmp1 < b2[x]) ||
							(tmp2 > b0[x] && tmp2 > b2[x]);
						if (vc) {
							sump[index]++;
						}

						x++;
						if (!(x & 3)) x += 4;
					}
				}
				currbot0 += cpitchtimes4;
				currbot2 += cpitchtimes4;
				currtop0 += cpitchtimes4;
				currtop2 += cpitchtimes4;
				currtop4 += cpitchtimes4;
				a0 += apitchtimes4;
				a2 += apitchtimes4;
				b0 += bpitchtimes4;
				b2 += bpitchtimes4;
				b4 += bpitchtimes4;
			}
		}
	}

	if (true /*post != POST_NONE*/) {
		highest_sump = 0;
		for (y = 0; y < yblocks; y++) {
			for (x = 0; x < xblocks; x++) {
				if (sump[y * xblocks + x] > highest_sump) {
					highest_sump = sump[y * xblocks + x];
				}
			}
		}
		highest_sumc = 0;
		for (y = 0; y < yblocks; y++) {
			for (x = 0; x < xblocks; x++) {
				if (sumc[y * xblocks + x] > highest_sumc) {
					highest_sumc = sumc[y * xblocks + x];
				}
			}
		}
	}

	delete [] sumc;
	delete [] sump;

	metrics.c = c;
	metrics.p = p;
	metrics.highest_sumc = highest_sumc;
	metrics.highest_sump = highest_sump;
}

static const VSFrame *VS_CC dmetricsGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    DMetricsData *d = reinterpret_cast<DMetricsData *>(instanceData);

    if (activationReason == arInitial) {
		if (n > 0)
			vsapi->requestFrameFilter(n - 1, d->node, frameCtx);
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *frameP = vsapi->getFrameFilter(std::max(n - 1, 0), d->node, frameCtx);
		const VSFrame *frameC = vsapi->getFrameFilter(n, d->node, frameCtx);
        
		DMetricsSet metrics;
		calculateTelecideMetrics(frameC, frameP, d->settings, metrics, vsapi);

		VSFrame *frameDst = vsapi->copyFrame(frameC, core);
		VSMap *props = vsapi->getFramePropertiesRW(frameDst);

		vsapi->mapSetInt(props, d->mmprop.c_str(), metrics.p, maAppend);
		vsapi->mapSetInt(props, d->mmprop.c_str(), metrics.c, maAppend);

		vsapi->mapSetInt(props, d->vmprop.c_str(), metrics.highest_sump, maAppend);
		vsapi->mapSetInt(props, d->vmprop.c_str(), metrics.highest_sumc, maAppend);

		vsapi->freeFrame(frameP);
		vsapi->freeFrame(frameC);

        return frameDst;
    }

    return nullptr;
}

static void VS_CC dmetricsFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	DMetricsData *d = reinterpret_cast<DMetricsData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

static void VS_CC dmetricsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	std::unique_ptr<DMetricsData> d(new DMetricsData());
	int err;
	d->settings.tff = !!vsapi->mapGetInt(in, "tff", 0, &err);
	d->settings.chroma = !!vsapi->mapGetInt(in, "chroma", 0, &err);
	if (err)
		d->settings.chroma = true;
	d->settings.nt = vsapi->mapGetIntSaturated(in, "nt", 0, &err);
	if (err)
		d->settings.nt = 10;
	d->settings.y0 = vsapi->mapGetIntSaturated(in, "y0", 0, &err);
	d->settings.y1 = vsapi->mapGetIntSaturated(in, "y1", 0, &err);

	d->mmprop = "MMetric";
	d->vmprop = "VMetric";

	const char *prefix = vsapi->mapGetData(in, "prefix", 0, &err);
	if (prefix) {
		d->mmprop = prefix + d->mmprop;
		d->vmprop = prefix + d->vmprop;
	}

	if (d->settings.nt < 0) {
		vsapi->mapSetError(out, "DMetrics: nt must be 0 or bigger");
		return;
	}

	if (d->settings.y0 < 0 || d->settings.y1 < 0 || d->settings.y0 > d->settings.y1) {
		vsapi->mapSetError(out, "DMetrics: invalid y0/y1 values");
		return;
	}

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d->vi = vsapi->getVideoInfo(d->node);
	if (vsapi->queryVideoFormatID(d->vi->format.colorFamily, d->vi->format.sampleType, d->vi->format.bitsPerSample, d->vi->format.subSamplingW, d->vi->format.subSamplingH, core) != pfYUV420P8) {
		vsapi->freeNode(d->node);
		vsapi->mapSetError(out, "DMetrics: only YUV420P8 supported");
		return;
	}

	VSFilterDependency deps[] = { {d->node, rpGeneral} };
    vsapi->createVideoFilter(out, "DMetrics", d->vi, dmetricsGetFrame, dmetricsFree, fmParallel, deps, 1, d.get(), core);
	d.release();
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.vapoursynth.dmetrics", "dmetrics", "Decomb Metrics", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("DMetrics", "clip:vnode;tff:int:opt;chroma:int:opt;nt:int:opt;y0:int:opt;y1:int:opt;prefix:data:opt;", "clip:vnode;", dmetricsCreate, nullptr, plugin);
}