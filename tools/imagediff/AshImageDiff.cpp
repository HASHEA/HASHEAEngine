// AshImageDiff（SDD-2026-07-07-render-gate T3）：RenderGate 图像对比工具。
// 加载两张 PNG，输出灰度 SSIM（11x11 高斯窗口，标准参数）、逐像素统计与可选 diff 热力图。
// 退出码：0 = PASS，1 = FAIL（低于阈值或尺寸不匹配），2 = 用法/IO 错误。
#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
	struct Image
	{
		int width = 0;
		int height = 0;
		std::vector<uint8_t> rgba;
	};

	bool load_image(const char* path, Image& out_image, std::string& out_error)
	{
		int channels = 0;
		stbi_uc* pixels = stbi_load(path, &out_image.width, &out_image.height, &channels, 4);
		if (!pixels)
		{
			out_error = std::string("failed to load '") + path + "': " + (stbi_failure_reason() ? stbi_failure_reason() : "unknown");
			return false;
		}
		out_image.rgba.assign(pixels, pixels + static_cast<size_t>(out_image.width) * out_image.height * 4);
		stbi_image_free(pixels);
		return true;
	}

	std::vector<float> to_luma(const Image& image)
	{
		const size_t pixel_count = static_cast<size_t>(image.width) * image.height;
		std::vector<float> luma(pixel_count);
		for (size_t i = 0; i < pixel_count; ++i)
		{
			const uint8_t* p = &image.rgba[i * 4];
			luma[i] = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
		}
		return luma;
	}

	// 分离高斯模糊（sigma=1.5，11 taps），边界 clamp。
	void gaussian_blur(const std::vector<float>& src, std::vector<float>& dst, int width, int height)
	{
		constexpr int k_radius = 5;
		float kernel[2 * k_radius + 1];
		float kernel_sum = 0.0f;
		for (int i = -k_radius; i <= k_radius; ++i)
		{
			const float value = std::exp(-(static_cast<float>(i) * i) / (2.0f * 1.5f * 1.5f));
			kernel[i + k_radius] = value;
			kernel_sum += value;
		}
		for (float& value : kernel)
		{
			value /= kernel_sum;
		}

		std::vector<float> temp(src.size());
		for (int y = 0; y < height; ++y)
		{
			const float* row = &src[static_cast<size_t>(y) * width];
			float* out_row = &temp[static_cast<size_t>(y) * width];
			for (int x = 0; x < width; ++x)
			{
				float accum = 0.0f;
				for (int i = -k_radius; i <= k_radius; ++i)
				{
					const int sx = std::clamp(x + i, 0, width - 1);
					accum += row[sx] * kernel[i + k_radius];
				}
				out_row[x] = accum;
			}
		}

		dst.resize(src.size());
		for (int y = 0; y < height; ++y)
		{
			float* out_row = &dst[static_cast<size_t>(y) * width];
			for (int x = 0; x < width; ++x)
			{
				float accum = 0.0f;
				for (int i = -k_radius; i <= k_radius; ++i)
				{
					const int sy = std::clamp(y + i, 0, height - 1);
					accum += temp[static_cast<size_t>(sy) * width + x] * kernel[i + k_radius];
				}
				out_row[x] = accum;
			}
		}
	}

	double compute_ssim(const std::vector<float>& luma_a, const std::vector<float>& luma_b, int width, int height)
	{
		const size_t pixel_count = static_cast<size_t>(width) * height;
		std::vector<float> a_sq(pixel_count);
		std::vector<float> b_sq(pixel_count);
		std::vector<float> a_b(pixel_count);
		for (size_t i = 0; i < pixel_count; ++i)
		{
			a_sq[i] = luma_a[i] * luma_a[i];
			b_sq[i] = luma_b[i] * luma_b[i];
			a_b[i] = luma_a[i] * luma_b[i];
		}

		std::vector<float> mu_a, mu_b, mu_a_sq, mu_b_sq, mu_a_b;
		gaussian_blur(luma_a, mu_a, width, height);
		gaussian_blur(luma_b, mu_b, width, height);
		gaussian_blur(a_sq, mu_a_sq, width, height);
		gaussian_blur(b_sq, mu_b_sq, width, height);
		gaussian_blur(a_b, mu_a_b, width, height);

		constexpr double k_c1 = (0.01 * 255.0) * (0.01 * 255.0);
		constexpr double k_c2 = (0.03 * 255.0) * (0.03 * 255.0);
		double ssim_sum = 0.0;
		for (size_t i = 0; i < pixel_count; ++i)
		{
			const double mean_a = mu_a[i];
			const double mean_b = mu_b[i];
			const double variance_a = mu_a_sq[i] - mean_a * mean_a;
			const double variance_b = mu_b_sq[i] - mean_b * mean_b;
			const double covariance = mu_a_b[i] - mean_a * mean_b;
			const double numerator = (2.0 * mean_a * mean_b + k_c1) * (2.0 * covariance + k_c2);
			const double denominator = (mean_a * mean_a + mean_b * mean_b + k_c1) * (variance_a + variance_b + k_c2);
			ssim_sum += numerator / denominator;
		}
		return ssim_sum / static_cast<double>(pixel_count);
	}

	// 黑→蓝→绿→黄→红 ramp，输入 0-255 diff 幅度。
	void heatmap_color(float magnitude, uint8_t out_rgb[3])
	{
		const float t = std::clamp(magnitude / 255.0f, 0.0f, 1.0f);
		float r = 0.0f, g = 0.0f, b = 0.0f;
		if (t < 0.25f)
		{
			b = t / 0.25f;
		}
		else if (t < 0.5f)
		{
			b = 1.0f - (t - 0.25f) / 0.25f;
			g = (t - 0.25f) / 0.25f;
		}
		else if (t < 0.75f)
		{
			g = 1.0f;
			r = (t - 0.5f) / 0.25f;
		}
		else
		{
			r = 1.0f;
			g = 1.0f - (t - 0.75f) / 0.25f;
		}
		out_rgb[0] = static_cast<uint8_t>(r * 255.0f + 0.5f);
		out_rgb[1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
		out_rgb[2] = static_cast<uint8_t>(b * 255.0f + 0.5f);
	}

	void print_usage()
	{
		std::fprintf(stderr,
			"Usage: AshImageDiff <imageA.png> <imageB.png> [options]\n"
			"Options:\n"
			"  --ssim-threshold=<0..1>  FAIL when SSIM is below this value (default 1.0)\n"
			"  --heatmap=<path.png>     write a per-pixel diff heatmap PNG\n"
			"Exit codes: 0 = PASS, 1 = FAIL, 2 = usage/IO error\n");
	}
}

int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		print_usage();
		return 2;
	}

	const char* path_a = argv[1];
	const char* path_b = argv[2];
	double ssim_threshold = 1.0;
	std::string heatmap_path;
	for (int i = 3; i < argc; ++i)
	{
		const std::string argument = argv[i] ? argv[i] : "";
		constexpr const char* k_threshold_prefix = "--ssim-threshold=";
		constexpr const char* k_heatmap_prefix = "--heatmap=";
		if (argument.rfind(k_threshold_prefix, 0) == 0)
		{
			ssim_threshold = std::strtod(argument.c_str() + std::string(k_threshold_prefix).size(), nullptr);
			if (ssim_threshold < 0.0 || ssim_threshold > 1.0)
			{
				std::fprintf(stderr, "Invalid --ssim-threshold value: %s\n", argument.c_str());
				return 2;
			}
		}
		else if (argument.rfind(k_heatmap_prefix, 0) == 0)
		{
			heatmap_path = argument.substr(std::string(k_heatmap_prefix).size());
		}
		else
		{
			std::fprintf(stderr, "Unknown option: %s\n", argument.c_str());
			print_usage();
			return 2;
		}
	}

	Image image_a{};
	Image image_b{};
	std::string load_error;
	if (!load_image(path_a, image_a, load_error) || !load_image(path_b, image_b, load_error))
	{
		std::fprintf(stderr, "AshImageDiff: %s\n", load_error.c_str());
		return 2;
	}

	std::printf("image_a=%s\n", path_a);
	std::printf("image_b=%s\n", path_b);
	std::printf("width=%d\n", image_a.width);
	std::printf("height=%d\n", image_a.height);

	if (image_a.width != image_b.width || image_a.height != image_b.height)
	{
		std::printf("result=FAIL\n");
		std::fprintf(stderr, "AshImageDiff: size mismatch (%dx%d vs %dx%d).\n",
			image_a.width, image_a.height, image_b.width, image_b.height);
		return 1;
	}

	const size_t pixel_count = static_cast<size_t>(image_a.width) * image_a.height;
	uint32_t max_abs_diff = 0;
	uint64_t total_abs_diff = 0;
	uint64_t diff_pixel_count = 0;
	std::vector<uint8_t> heatmap_pixels;
	if (!heatmap_path.empty())
	{
		heatmap_pixels.resize(pixel_count * 3);
	}
	for (size_t i = 0; i < pixel_count; ++i)
	{
		const uint8_t* pa = &image_a.rgba[i * 4];
		const uint8_t* pb = &image_b.rgba[i * 4];
		uint32_t pixel_max_diff = 0;
		for (int channel = 0; channel < 3; ++channel)
		{
			const uint32_t diff = static_cast<uint32_t>(std::abs(static_cast<int>(pa[channel]) - static_cast<int>(pb[channel])));
			pixel_max_diff = std::max(pixel_max_diff, diff);
			total_abs_diff += diff;
		}
		max_abs_diff = std::max(max_abs_diff, pixel_max_diff);
		if (pixel_max_diff > 0)
		{
			++diff_pixel_count;
		}
		if (!heatmap_pixels.empty())
		{
			heatmap_color(static_cast<float>(pixel_max_diff), &heatmap_pixels[i * 3]);
		}
	}

	const std::vector<float> luma_a = to_luma(image_a);
	const std::vector<float> luma_b = to_luma(image_b);
	const double ssim = compute_ssim(luma_a, luma_b, image_a.width, image_a.height);

	std::printf("ssim=%.6f\n", ssim);
	std::printf("ssim_threshold=%.6f\n", ssim_threshold);
	std::printf("max_abs_diff=%u\n", max_abs_diff);
	std::printf("mean_abs_diff=%.6f\n", static_cast<double>(total_abs_diff) / (static_cast<double>(pixel_count) * 3.0));
	std::printf("diff_pixel_count=%llu\n", static_cast<unsigned long long>(diff_pixel_count));
	std::printf("diff_pixel_ratio=%.6f\n", static_cast<double>(diff_pixel_count) / static_cast<double>(pixel_count));

	if (!heatmap_path.empty())
	{
		if (stbi_write_png(heatmap_path.c_str(), image_a.width, image_a.height, 3, heatmap_pixels.data(), image_a.width * 3) == 0)
		{
			std::fprintf(stderr, "AshImageDiff: failed to write heatmap '%s'.\n", heatmap_path.c_str());
			return 2;
		}
		std::printf("heatmap=%s\n", heatmap_path.c_str());
	}

	const bool passed = ssim >= ssim_threshold;
	std::printf("result=%s\n", passed ? "PASS" : "FAIL");
	return passed ? 0 : 1;
}
