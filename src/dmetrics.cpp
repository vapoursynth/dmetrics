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
    VSNode *node = nullptr;
    const VSVideoInfo *vi = nullptr;
	std::string mmprop;
	std::string vmprop;
	DMetricsSettings settings = {};
};

struct DMetricsSet {
	unsigned int c;
	unsigned int p;
	unsigned int highest_sumc;
	unsigned int highest_sump;
};

static void calculateTelecideMetrics(const VSFrame *frameC, const VSFrame *frameP, const DMetricsSettings &settings, DMetricsSet &metrics, const VSAPI *vsapi) {
	const bool tff = settings.tff;
	const bool chroma = settings.chroma;
	const unsigned int nt = settings.nt;
	const size_t y0 = settings.y0;
	const size_t y1 = settings.y1;

	ptrdiff_t pitch = vsapi->getStride(frameC, 0);
	ptrdiff_t pitchUV = vsapi->getStride(frameC, 1);

	const uint8_t *fcrp = vsapi->getReadPtr(frameC, 0);
	const uint8_t *fcrpU = vsapi->getReadPtr(frameC, 1);
	const uint8_t *fcrpV = vsapi->getReadPtr(frameC, 2);

	const uint8_t *fprp = vsapi->getReadPtr(frameP, 0);
	const uint8_t *fprpU = vsapi->getReadPtr(frameP, 1);
	const uint8_t *fprpV = vsapi->getReadPtr(frameP, 2);

	const size_t w = vsapi->getFrameWidth(frameC, 0);
	const size_t h = vsapi->getFrameHeight(frameC, 0);
	const size_t wover2 = w / 2;
	const size_t hover2 = h / 2;
	const size_t xblocks = (w + BLKSIZE - 1) / BLKSIZE;
	const size_t yblocks = (h + BLKSIZE - 1) / BLKSIZE;

	constexpr int T = 4;

	size_t numblocks = xblocks * yblocks;

	unsigned int *sumc = new unsigned int[numblocks]();
	unsigned int *sump = new unsigned int[numblocks]();

	/* Find the best field match. Subsample the frames for speed. */
	const uint8_t *currbot0 = fcrp + pitch;
	const uint8_t *currbot2 = fcrp + 3 * pitch;
	const uint8_t *currtop0 = fcrp;
	const uint8_t *currtop2 = fcrp + 2 * pitch;
	const uint8_t *currtop4 = fcrp + 4 * pitch;
	const uint8_t *prevbot0 = fprp + pitch;
	const uint8_t *prevbot2 = fprp + 3 * pitch;
	const uint8_t *prevtop0 = fprp;
	const uint8_t *prevtop2 = fprp + 2 * pitch;
	const uint8_t *prevtop4 = fprp + 4 * pitch;

	const uint8_t *a0;
	const uint8_t *a2;
	const uint8_t *b0;
	const uint8_t *b2;
	const uint8_t *b4;

	if (tff) {
		a0 = prevbot0;
		a2 = prevbot2;
		b0 = currtop0;
		b2 = currtop2;
		b4 = currtop4;
	} else {
		a0 = currbot0;
		a2 = currbot2;
		b0 = prevtop0;
		b2 = prevtop2;
		b4 = prevtop4;
	}

	unsigned int c = 0;
	unsigned int p = 0;

	// Calculate the field match and film/video metrics.
	constexpr size_t skip = 1;
	for (size_t y = 0; y < h - 4; y += 4) {
		/* Exclusion band. Good for ignoring subtitles. */
		if (y0 == y1 || y < y0 || y > y1) {
			for (size_t x = 0; x < w;) {
				size_t index = (y / BLKSIZE) * xblocks + x / BLKSIZE;

				// Test combination with current frame.
				int tmp1 = ((int)currbot0[x] + (int)currbot2[x]);
				//				diff = abs((long)currtop0[x] - (tmp1 >> 1));
				unsigned int diff = abs((((int)currtop0[x] + (int)currtop2[x] + (int)currtop4[x])) - (tmp1 >> 1) - tmp1);
				if (diff > nt) {
					c += diff;
				}

				tmp1 = currbot0[x] + T;
				int tmp2 = currbot0[x] - T;
				bool vc = (tmp1 < currtop0[x] && tmp1 < currtop2[x]) ||
					(tmp2 > currtop0[x] && tmp2 > currtop2[x]);
				if (vc) {
					sumc[index]++;
				}

				// Test combination with previous frame.
				tmp1 = ((int)a0[x] + (int)a2[x]);
				diff = abs((((int)b0[x] + (int)b2[x] + (int)b4[x])) - (tmp1 >> 1) - tmp1);
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
		currbot0 += 4 * pitch;
		currbot2 += 4 * pitch;
		currtop0 += 4 * pitch;
		currtop2 += 4 * pitch;
		currtop4 += 4 * pitch;
		a0 += 4 * pitch;
		a2 += 4 * pitch;
		b0 += 4 * pitch;
		b2 += 4 * pitch;
		b4 += 4 * pitch;
	}

	if (chroma) {
		for (int z = 0; z < 2; z++) {
			// Do the same for the U plane.
			if (z == 0) {
				currbot0 = fcrpU + pitchUV;
				currbot2 = fcrpU + 3 * pitchUV;
				currtop0 = fcrpU;
				currtop2 = fcrpU + 2 * pitchUV;
				currtop4 = fcrpU + 4 * pitchUV;
				prevbot0 = fprpU + pitchUV;
				prevbot2 = fprpU + 3 * pitchUV;
				prevtop0 = fprpU;
				prevtop2 = fprpU + 2 * pitchUV;
				prevtop4 = fprpU + 4 * pitchUV;
			} else {
				currbot0 = fcrpV + pitchUV;
				currbot2 = fcrpV + 3 * pitchUV;
				currtop0 = fcrpV;
				currtop2 = fcrpV + 2 * pitchUV;
				currtop4 = fcrpV + 4 * pitchUV;
				prevbot0 = fprpV + pitchUV;
				prevbot2 = fprpV + 3 * pitchUV;
				prevtop0 = fprpV;
				prevtop2 = fprpV + 2 * pitchUV;
				prevtop4 = fprpV + 4 * pitchUV;
			}
			if (tff) {
				a0 = prevbot0;
				a2 = prevbot2;
				b0 = currtop0;
				b2 = currtop2;
				b4 = currtop4;
			} else {
				a0 = currbot0;
				a2 = currbot2;
				b0 = prevtop0;
				b2 = prevtop2;
				b4 = prevtop4;
			}

			for (size_t y = 0; y < hover2 - 4; y += 4) {
				/* Exclusion band. Good for ignoring subtitles. */
				if (y0 == y1 || y < y0 / 2 || y > y1 / 2) {
					for (size_t x = 0; x < wover2;) {
						size_t index = (y / BLKSIZE) * xblocks + x / BLKSIZE;
						// Test combination with current frame.
						int tmp1 = ((int)currbot0[x] + (int)currbot2[x]);
						unsigned int diff = abs((((int)currtop0[x] + (int)currtop2[x] + (int)currtop4[x])) - (tmp1 >> 1) - tmp1);
						if (diff > nt) {
							c += diff;
						}

						tmp1 = currbot0[x] + T;
						int tmp2 = currbot0[x] - T;
						bool vc = (tmp1 < currtop0[x] && tmp1 < currtop2[x]) ||
							(tmp2 > currtop0[x] && tmp2 > currtop2[x]);
						if (vc) {
							sumc[index]++;
						}

						// Test combination with previous frame.
						tmp1 = ((int)a0[x] + (int)a2[x]);
						diff = abs((((int)b0[x] + (int)b2[x] + (int)b4[x])) - (tmp1 >> 1) - tmp1);
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
				currbot0 += 4 * pitchUV;
				currbot2 += 4 * pitchUV;
				currtop0 += 4 * pitchUV;
				currtop2 += 4 * pitchUV;
				currtop4 += 4 * pitchUV;
				a0 += 4 * pitchUV;
				a2 += 4 * pitchUV;
				b0 += 4 * pitchUV;
				b2 += 4 * pitchUV;
				b4 += 4 * pitchUV;
			}
		}
	}

	unsigned int highest_sumc = *std::max_element(sumc, sumc + numblocks);
	unsigned int highest_sump = *std::max_element(sump, sump + numblocks);

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