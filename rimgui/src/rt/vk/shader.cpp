#include <rimgui/rt/vk/shader.h>

#include <shaderc/shaderc.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#undef WIN32_LEAN_AND_MEAN // DXC's interfaces are COM: IUnknown, IStream and BSTR come from the full header.
#include <windows.h>
#include <dxc/dxcapi.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace rimgui::rt::vk {

namespace fs = std::filesystem;

namespace {

std::string read_text(const fs::path &path, bool &ok)
{
	std::ifstream in(path, std::ios::binary);
	ok = static_cast<bool>(in);
	std::ostringstream s;
	s << in.rdbuf();
	return s.str();
}

fs::path find_file(const std::string &file, const fs::path &beside, const std::vector<fs::path> &folders)
{
	std::error_code ignored;
	if (fs::path(file).is_absolute() && fs::exists(file, ignored)) {
		return file;
	}
	if (!beside.empty() && fs::exists(beside / file, ignored)) {
		return beside / file;
	}
	for (const auto &folder : folders) {
		if (fs::exists(folder / file, ignored)) {
			return folder / file;
		}
	}
	return {};
}

// "post.hlsl:Bloom" -> {"post.hlsl", "Bloom"}; a drive letter's colon ("C:/x.glsl") is not a separator.
std::pair<std::string, std::string> split_entry(const std::string &spec)
{
	const auto colon = spec.rfind(':');
	if (colon == std::string::npos || colon <= 1 || spec.find_first_of("/\\", colon) != std::string::npos) {
		return {spec, "main"};
	}
	return {spec.substr(0, colon), spec.substr(colon + 1)};
}

// Paths in compiler messages shortened to file names, so a message reads "brdf.glsl:9: error ...".
std::string shorten(std::string text, const std::vector<fs::path> &files)
{
	for (const auto &f : files) {
		for (const std::string &full : {f.string(), f.generic_string()}) {
			const std::string name = f.filename().string();
			for (std::size_t at = text.find(full); at != std::string::npos; at = text.find(full, at + name.size())) {
				text.replace(at, full.size(), name);
			}
		}
	}
	return text;
}

// ---- GLSL through shaderc --------------------------------------------------------------------------------------------

struct Includer {
	std::vector<fs::path> folders;
	std::vector<fs::path> *files;
	struct Result {
		shaderc_include_result result{};
		std::string name, content;
	};
};

shaderc_include_result *resolve(void *user, const char *requested, int, const char *requesting, size_t)
{
	auto *in = static_cast<Includer *>(user);
	auto *r = new Includer::Result();
	const fs::path found = find_file(requested, fs::path(requesting).parent_path(), in->folders);
	bool ok = false;
	if (!found.empty()) {
		r->content = read_text(found, ok);
	}
	if (ok) {
		r->name = found.string();
		in->files->push_back(found);
	} else {
		r->name.clear();
		r->content = std::string("cannot find \"") + requested + "\" beside " + fs::path(requesting).filename().string() + " or in the shader folders";
	}
	r->result.source_name = r->name.c_str();
	r->result.source_name_length = r->name.size();
	r->result.content = r->content.c_str();
	r->result.content_length = r->content.size();
	r->result.user_data = r;
	return &r->result;
}

void release(void *, shaderc_include_result *result)
{
	delete static_cast<Includer::Result *>(result->user_data);
}

shaderc_shader_kind kind_of(Stage s)
{
	switch (s) {
	case Stage::Vertex: return shaderc_vertex_shader;
	case Stage::Fragment: return shaderc_fragment_shader;
	case Stage::Compute: return shaderc_compute_shader;
	case Stage::Task: return shaderc_task_shader;
	case Stage::Mesh: return shaderc_mesh_shader;
	case Stage::RayGen: return shaderc_raygen_shader;
	case Stage::Miss: return shaderc_miss_shader;
	case Stage::ClosestHit: return shaderc_closesthit_shader;
	case Stage::AnyHit: return shaderc_anyhit_shader;
	case Stage::Intersection: return shaderc_intersection_shader;
	case Stage::Callable: return shaderc_callable_shader;
	}
	return shaderc_glsl_infer_from_source;
}

Compiled compile_glsl(const fs::path &path, const std::string &source, Stage stage, const std::string &entry, const std::vector<fs::path> &folders)
{
	Compiled out;
	out.files.push_back(path);
	static shaderc_compiler_t compiler = shaderc_compiler_initialize();
	shaderc_compile_options_t options = shaderc_compile_options_initialize();
	shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
	shaderc_compile_options_set_target_spirv(options, shaderc_spirv_version_1_6);
	shaderc_compile_options_set_source_language(options, shaderc_source_language_glsl);
	shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_performance);
	shaderc_compile_options_set_generate_debug_info(options); // Names in validation messages and GPU debuggers.
	shaderc_compile_options_add_macro_definition(options, "RIMGUI_VULKAN", 13, "1", 1);
	Includer includer{folders, &out.files};
	shaderc_compile_options_set_include_callbacks(options, resolve, release, &includer);
	const std::string name = path.string();
	shaderc_compilation_result_t result =
		shaderc_compile_into_spv(compiler, source.data(), source.size(), kind_of(stage), name.c_str(), entry.c_str(), options);
	if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
		out.error = shorten(shaderc_result_get_error_message(result), out.files);
	} else {
		const auto *words = reinterpret_cast<const uint32_t *>(shaderc_result_get_bytes(result));
		out.spirv.assign(words, words + shaderc_result_get_length(result) / 4);
	}
	shaderc_result_release(result);
	shaderc_compile_options_release(options);
	return out;
}

// ---- HLSL through DXC ------------------------------------------------------------------------------------------------

#ifdef _WIN32
struct RecordingIncluder final : IDxcIncludeHandler {
	IDxcUtils *utils = nullptr;
	IDxcIncludeHandler *fallback = nullptr;
	std::vector<fs::path> *files = nullptr;
	std::vector<fs::path> folders;
	fs::path beside;
	ULONG references = 1;

	HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR filename, IDxcBlob **include) override
	{
		const fs::path requested(filename);
		fs::path found = find_file(requested.string(), beside, folders);
		if (found.empty()) {
			// DXC passes "./name" relative to the including file's folder.
			found = find_file(requested.filename().string(), beside, folders);
		}
		if (found.empty()) {
			*include = nullptr;
			return E_FAIL;
		}
		files->push_back(found);
		IDxcBlobEncoding *blob = nullptr;
		const HRESULT hr = utils->LoadFile(found.wstring().c_str(), nullptr, &blob);
		*include = blob;
		return hr;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
	{
		if (riid == __uuidof(IDxcIncludeHandler) || riid == __uuidof(IUnknown)) {
			*object = this;
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
	ULONG STDMETHODCALLTYPE Release() override { return --references; }
};

const wchar_t *profile_of(Stage s)
{
	switch (s) {
	case Stage::Vertex: return L"vs_6_6";
	case Stage::Fragment: return L"ps_6_6";
	case Stage::Compute: return L"cs_6_6";
	case Stage::Task: return L"as_6_6";
	case Stage::Mesh: return L"ms_6_6";
	default: return L"lib_6_6";
	}
}

Compiled compile_hlsl(const fs::path &path, const std::string &source, Stage stage, const std::string &entry, const std::vector<fs::path> &folders)
{
	Compiled out;
	out.files.push_back(path);
	static IDxcCompiler3 *compiler = nullptr;
	static IDxcUtils *utils = nullptr;
	if (!compiler) {
		if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))) || FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)))) {
			compiler = nullptr;
			out.error = path.filename().string() + ": DXC could not start (dxcompiler.dll must be beside the program)";
			return out;
		}
	}
	RecordingIncluder includer;
	includer.utils = utils;
	includer.files = &out.files;
	includer.folders = folders;
	includer.beside = path.parent_path();
	std::vector<std::wstring> args{path.wstring(), L"-spirv", L"-fspv-target-env=vulkan1.3", L"-fvk-use-scalar-layout", L"-HV", L"2021", L"-O3",
		L"-Zi", L"-T", profile_of(stage), L"-D", L"RIMGUI_VULKAN=1"};
	if (stage < Stage::RayGen) {
		args.push_back(L"-E");
		args.push_back(std::wstring(entry.begin(), entry.end()));
	}
	for (const auto &f : folders) {
		args.push_back(L"-I");
		args.push_back(f.wstring());
	}
	std::vector<LPCWSTR> raw;
	for (const auto &a : args) {
		raw.push_back(a.c_str());
	}
	DxcBuffer buffer{source.data(), source.size(), DXC_CP_UTF8};
	IDxcResult *result = nullptr;
	if (FAILED(compiler->Compile(&buffer, raw.data(), UINT32(raw.size()), &includer, IID_PPV_ARGS(&result))) || !result) {
		out.error = path.filename().string() + ": DXC failed to run";
		return out;
	}
	HRESULT status = E_FAIL;
	result->GetStatus(&status);
	IDxcBlobUtf8 *errors = nullptr;
	result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
	if (FAILED(status)) {
		out.error = shorten(errors && errors->GetStringLength() ? errors->GetStringPointer() : "DXC failed", out.files);
	} else {
		IDxcBlob *object = nullptr;
		result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
		if (object) {
			const auto *words = static_cast<const uint32_t *>(object->GetBufferPointer());
			out.spirv.assign(words, words + object->GetBufferSize() / 4);
			object->Release();
		}
	}
	if (errors) errors->Release();
	result->Release();
	return out;
}
#endif

double now_seconds()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

VkShaderStageFlagBits vk_stage(Stage s)
{
	switch (s) {
	case Stage::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
	case Stage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
	case Stage::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
	case Stage::Task: return VK_SHADER_STAGE_TASK_BIT_EXT;
	case Stage::Mesh: return VK_SHADER_STAGE_MESH_BIT_EXT;
	case Stage::RayGen: return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	case Stage::Miss: return VK_SHADER_STAGE_MISS_BIT_KHR;
	case Stage::ClosestHit: return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
	case Stage::AnyHit: return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	case Stage::Intersection: return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
	case Stage::Callable: return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
	}
	return VK_SHADER_STAGE_ALL;
}

struct Module {
	VkShaderModule module = VK_NULL_HANDLE;
	VkShaderStageFlagBits stage{};
	std::string entry;
};

} // namespace

Compiled compile(const std::string &spec, Stage stage, const std::vector<fs::path> &folders)
{
	const auto [file, entry] = split_entry(spec);
	Compiled out;
	const fs::path path = find_file(file, {}, folders);
	if (path.empty()) {
		out.error = file + ": not found in the shader folders";
		return out;
	}
	bool ok = false;
	const std::string source = read_text(path, ok);
	if (!ok) {
		out.error = file + ": cannot be read";
		out.files.push_back(path);
		return out;
	}
	std::string ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	if (ext == ".hlsl") {
#ifdef _WIN32
		return compile_hlsl(path, source, stage, entry, folders);
#else
		out.error = file + ": HLSL is compiled with DXC, which the kit uses on Windows only so far";
		out.files.push_back(path);
		return out;
#endif
	}
	return compile_glsl(path, source, stage, entry, folders);
}

// ---- Pipeline --------------------------------------------------------------------------------------------------------

Pipeline::~Pipeline()
{
	if (pipeline_ && Gpu::get()) {
		VkDevice device = Gpu::get()->device();
		VkPipeline p = pipeline_;
		vulkan::defer([device, p] { vkDestroyPipeline(device, p, nullptr); });
	}
}

void Pipeline::bind(VkCommandBuffer cb, const void *constants, uint32_t size) const
{
	vkCmdBindPipeline(cb, bind_point_, pipeline_);
	if (kind_ == Kind::Graphics) {
		vkCmdSetCullMode(cb, graphics_.cull);
	}
	Gpu::get()->bind(cb, bind_point_, constants, size);
}

void Pipeline::trace(VkCommandBuffer cb, uint32_t width, uint32_t height, uint32_t depth) const
{
	vkCmdTraceRaysKHR(cb, &raygen_, &miss_, &hit_, &callable_, width, height, depth);
}

// ---- ShaderLibrary ---------------------------------------------------------------------------------------------------

ShaderLibrary::ShaderLibrary(std::vector<fs::path> folders) : folders_(std::move(folders)) {}
ShaderLibrary::~ShaderLibrary() = default;

void ShaderLibrary::add_folder(fs::path folder)
{
	folders_.push_back(std::move(folder));
}

Pipeline &ShaderLibrary::graphics(const std::string &name, const GraphicsDesc &desc)
{
	for (auto &p : pipelines_) {
		if (p->name_ == name) {
			return *p;
		}
	}
	std::unique_ptr<Pipeline> p(new Pipeline());
	p->name_ = name;
	p->kind_ = Pipeline::Kind::Graphics;
	p->graphics_ = desc;
	p->bind_point_ = VK_PIPELINE_BIND_POINT_GRAPHICS;
	build(*p);
	pipelines_.push_back(std::move(p));
	return *pipelines_.back();
}

Pipeline &ShaderLibrary::compute(const std::string &name, const std::string &file)
{
	for (auto &p : pipelines_) {
		if (p->name_ == name) {
			return *p;
		}
	}
	std::unique_ptr<Pipeline> p(new Pipeline());
	p->name_ = name;
	p->kind_ = Pipeline::Kind::Compute;
	p->compute_ = file;
	p->bind_point_ = VK_PIPELINE_BIND_POINT_COMPUTE;
	build(*p);
	pipelines_.push_back(std::move(p));
	return *pipelines_.back();
}

Pipeline &ShaderLibrary::ray_tracing(const std::string &name, const RayTracingDesc &desc)
{
	for (auto &p : pipelines_) {
		if (p->name_ == name) {
			return *p;
		}
	}
	std::unique_ptr<Pipeline> p(new Pipeline());
	p->name_ = name;
	p->kind_ = Pipeline::Kind::RayTracing;
	p->ray_tracing_ = desc;
	p->bind_point_ = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
	build(*p);
	pipelines_.push_back(std::move(p));
	return *pipelines_.back();
}

bool ShaderLibrary::build(Pipeline &p)
{
	Gpu *gpu = Gpu::get();
	const auto &ctx = *vulkan::context();
	VkDevice device = ctx.device;
	std::vector<std::pair<std::string, Stage>> wanted;
	if (p.kind_ == Pipeline::Kind::Compute) {
		wanted.push_back({p.compute_, Stage::Compute});
	} else if (p.kind_ == Pipeline::Kind::Graphics) {
		const GraphicsDesc &d = p.graphics_;
		if (!d.mesh.empty()) {
			if (!d.task.empty()) wanted.push_back({d.task, Stage::Task});
			wanted.push_back({d.mesh, Stage::Mesh});
		} else {
			wanted.push_back({d.vertex, Stage::Vertex});
		}
		if (!d.fragment.empty()) wanted.push_back({d.fragment, Stage::Fragment});
	} else {
		const RayTracingDesc &d = p.ray_tracing_;
		wanted.push_back({d.raygen, Stage::RayGen});
		for (const auto &m : d.miss) wanted.push_back({m, Stage::Miss});
		for (const auto &h : d.hit_groups) {
			if (!h.closest_hit.empty()) wanted.push_back({h.closest_hit, Stage::ClosestHit});
			if (!h.any_hit.empty()) wanted.push_back({h.any_hit, Stage::AnyHit});
			if (!h.intersection.empty()) wanted.push_back({h.intersection, Stage::Intersection});
		}
		for (const auto &c : d.callables) wanted.push_back({c, Stage::Callable});
	}

	// Feature checks first, so a missing feature reads as that and not as a compile error.
	std::string errors;
	if (p.kind_ == Pipeline::Kind::RayTracing && !ctx.features.ray_tracing_pipeline) {
		errors = "this GPU has no ray tracing pipelines (VK_KHR_ray_tracing_pipeline)\n";
	}
	if (p.kind_ == Pipeline::Kind::Graphics && !p.graphics_.mesh.empty() && !ctx.features.mesh_shader) {
		errors = "this GPU has no mesh shaders (VK_EXT_mesh_shader)\n";
	}
	std::vector<Module> modules;
	std::vector<fs::path> files;
	std::map<std::string, std::size_t> module_of; // By "file|stage", for the ray tracing groups.
	if (errors.empty()) {
		for (const auto &[spec, stage] : wanted) {
			Compiled c = compile(spec, stage, folders_);
			files.insert(files.end(), c.files.begin(), c.files.end());
			if (!c.error.empty()) {
				errors += c.error + (c.error.back() == '\n' ? "" : "\n");
				continue;
			}
			VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			info.codeSize = c.spirv.size() * 4;
			info.pCode = c.spirv.data();
			Module m;
			m.stage = vk_stage(stage);
			m.entry = stage >= Stage::RayGen && spec.find(':') == std::string::npos ? "main" : split_entry(spec).second;
			if (vkCreateShaderModule(device, &info, nullptr, &m.module) != VK_SUCCESS) {
				errors += spec + ": the driver refused its SPIR-V\n";
				continue;
			}
			module_of[spec + "|" + std::to_string(int(stage))] = modules.size();
			modules.push_back(m);
		}
	}
	p.files_ = files;
	std::error_code ignored;
	for (const auto &f : files) {
		const std::string key = f.string();
		const auto time = fs::last_write_time(f, ignored);
		auto it = std::find_if(times_.begin(), times_.end(), [&](const auto &t) { return t.first == key; });
		if (it == times_.end()) {
			times_.push_back({key, time});
		} else {
			it->second = time;
		}
	}

	VkPipeline made = VK_NULL_HANDLE;
	if (errors.empty()) {
		std::vector<VkPipelineShaderStageCreateInfo> stages;
		for (const auto &m : modules) {
			VkPipelineShaderStageCreateInfo s{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
			s.stage = m.stage;
			s.module = m.module;
			s.pName = m.entry.c_str();
			stages.push_back(s);
		}
		VkResult r = VK_SUCCESS;
		if (p.kind_ == Pipeline::Kind::Compute) {
			VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
			info.stage = stages[0];
			info.layout = gpu->pipeline_layout();
			r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &made);
		} else if (p.kind_ == Pipeline::Kind::Graphics) {
			const GraphicsDesc &d = p.graphics_;
			VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
			VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
			assembly.topology = d.topology;
			VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
			viewport.viewportCount = viewport.scissorCount = 1;
			VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
			raster.polygonMode = d.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
			raster.cullMode = d.cull;
			raster.frontFace = VK_FRONT_FACE_CLOCKWISE; // Vulkan counts facing with the opposite sign; with the unflipped viewport this keeps OpenGL's counter-clockwise front faces.
			raster.lineWidth = 1;
			raster.depthBiasEnable = d.depth_bias_constant != 0 || d.depth_bias_slope != 0;
			raster.depthBiasConstantFactor = d.depth_bias_constant;
			raster.depthBiasSlopeFactor = d.depth_bias_slope;
			VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
			multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
			depth.depthTestEnable = d.depth != VK_FORMAT_UNDEFINED && d.depth_test;
			depth.depthWriteEnable = d.depth != VK_FORMAT_UNDEFINED && d.depth_write;
			depth.depthCompareOp = d.depth_compare;
			std::vector<VkPipelineColorBlendAttachmentState> blends(d.colors.size());
			for (auto &b : blends) {
				b.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				b.blendEnable = d.blend != Blend::None;
				b.colorBlendOp = b.alphaBlendOp = VK_BLEND_OP_ADD;
				switch (d.blend) {
				case Blend::Alpha:
					b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
					b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					break;
				case Blend::Premultiplied:
					b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
					b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
					b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					break;
				case Blend::Additive:
					b.srcColorBlendFactor = b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
					b.srcAlphaBlendFactor = b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
					break;
				case Blend::None:
					break;
				}
			}
			VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
			blend.attachmentCount = uint32_t(blends.size());
			blend.pAttachments = blends.data();
			// Culling is dynamic so a scene can draw double-sided materials with the same pipeline (bind() sets the
			// pipeline's own).
			const VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_CULL_MODE};
			VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
			dynamic.dynamicStateCount = 3;
			dynamic.pDynamicStates = dynamic_states;
			VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
			rendering.colorAttachmentCount = uint32_t(d.colors.size());
			rendering.pColorAttachmentFormats = d.colors.data();
			rendering.depthAttachmentFormat = d.depth;
			VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
			info.pNext = &rendering;
			info.stageCount = uint32_t(stages.size());
			info.pStages = stages.data();
			const bool mesh = !d.mesh.empty();
			info.pVertexInputState = mesh ? nullptr : &vertex;
			info.pInputAssemblyState = mesh ? nullptr : &assembly;
			info.pViewportState = &viewport;
			info.pRasterizationState = &raster;
			info.pMultisampleState = &multisample;
			info.pDepthStencilState = &depth;
			info.pColorBlendState = &blend;
			info.pDynamicState = &dynamic;
			info.layout = gpu->pipeline_layout();
			r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &made);
		} else {
			const RayTracingDesc &d = p.ray_tracing_;
			std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
			const auto general = [&](const std::string &spec, Stage stage) {
				VkRayTracingShaderGroupCreateInfoKHR g{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
				g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
				g.generalShader = uint32_t(module_of.at(spec + "|" + std::to_string(int(stage))));
				g.closestHitShader = g.anyHitShader = g.intersectionShader = VK_SHADER_UNUSED_KHR;
				groups.push_back(g);
			};
			general(d.raygen, Stage::RayGen);
			for (const auto &m : d.miss) general(m, Stage::Miss);
			for (const auto &h : d.hit_groups) {
				VkRayTracingShaderGroupCreateInfoKHR g{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
				g.type = h.intersection.empty() ? VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR : VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
				g.generalShader = VK_SHADER_UNUSED_KHR;
				g.closestHitShader = h.closest_hit.empty() ? VK_SHADER_UNUSED_KHR : uint32_t(module_of.at(h.closest_hit + "|" + std::to_string(int(Stage::ClosestHit))));
				g.anyHitShader = h.any_hit.empty() ? VK_SHADER_UNUSED_KHR : uint32_t(module_of.at(h.any_hit + "|" + std::to_string(int(Stage::AnyHit))));
				g.intersectionShader = h.intersection.empty() ? VK_SHADER_UNUSED_KHR : uint32_t(module_of.at(h.intersection + "|" + std::to_string(int(Stage::Intersection))));
				groups.push_back(g);
			}
			for (const auto &c : d.callables) general(c, Stage::Callable);
			VkRayTracingPipelineCreateInfoKHR info{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
			info.stageCount = uint32_t(stages.size());
			info.pStages = stages.data();
			info.groupCount = uint32_t(groups.size());
			info.pGroups = groups.data();
			info.maxPipelineRayRecursionDepth = d.max_recursion;
			info.layout = gpu->pipeline_layout();
			r = vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &info, nullptr, &made);
			if (r == VK_SUCCESS) {
				// The shader binding table: one record per group, regions aligned as the GPU asks.
				VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
				VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
				props.pNext = &rt;
				vkGetPhysicalDeviceProperties2(ctx.physical_device, &props);
				const auto align = [](VkDeviceSize v, VkDeviceSize a) { return (v + a - 1) & ~(a - 1); };
				const VkDeviceSize handle = rt.shaderGroupHandleSize;
				const VkDeviceSize stride = align(handle, rt.shaderGroupHandleAlignment);
				const VkDeviceSize base = rt.shaderGroupBaseAlignment;
				const uint32_t miss_count = uint32_t(d.miss.size()), hit_count = uint32_t(d.hit_groups.size()), call_count = uint32_t(d.callables.size());
				const VkDeviceSize raygen_size = align(stride, base), miss_size = align(stride * miss_count, base),
								   hit_size = align(stride * hit_count, base), call_size = align(stride * call_count, base);
				std::vector<uint8_t> handles(std::size_t(handle) * groups.size());
				vkGetRayTracingShaderGroupHandlesKHR(device, made, 0, uint32_t(groups.size()), handles.size(), handles.data());
				std::vector<uint8_t> table(std::size_t(raygen_size + miss_size + hit_size + call_size + base), 0);
				std::size_t group = 0;
				const auto place = [&](VkDeviceSize offset, uint32_t count) {
					for (uint32_t i = 0; i < count; ++i, ++group) {
						std::memcpy(table.data() + offset + stride * i, handles.data() + handle * group, handle);
					}
				};
				place(0, 1);
				place(raygen_size, miss_count);
				place(raygen_size + miss_size, hit_count);
				place(raygen_size + miss_size + hit_size, call_count);
				auto sbt = Buffer::create(table.size(), VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR, Memory::Upload, p.name_ + " binding table");
				// The table's start aligned to the base alignment inside the buffer.
				const VkDeviceSize start = align(sbt->address(), base) - sbt->address();
				std::memcpy(static_cast<uint8_t *>(sbt->mapped()) + start, table.data(), table.size() - std::size_t(base));
				const VkDeviceAddress at = sbt->address() + start;
				p.raygen_ = {at, stride, stride};
				p.miss_ = {at + raygen_size, stride, stride * miss_count};
				p.hit_ = {at + raygen_size + miss_size, stride, stride * hit_count};
				p.callable_ = call_count ? VkStridedDeviceAddressRegionKHR{at + raygen_size + miss_size + hit_size, stride, stride * call_count} : VkStridedDeviceAddressRegionKHR{};
				p.sbt_ = std::move(sbt);
			}
		}
		if (r != VK_SUCCESS) {
			errors = p.name_ + ": the driver could not make the pipeline (" + vulkan::result_name(r) + ")\n";
			made = VK_NULL_HANDLE;
		}
	}
	for (const auto &m : modules) {
		vkDestroyShaderModule(device, m.module, nullptr);
	}
	if (!errors.empty()) {
		p.error_ = errors;
		if (sink_) {
			sink_(false, p.name_ + ": " + errors);
		}
		return false;
	}
	const bool was_broken = !p.error_.empty();
	if (p.pipeline_) {
		VkPipeline old = p.pipeline_;
		vulkan::defer([device, old] { vkDestroyPipeline(device, old, nullptr); });
	}
	p.pipeline_ = made;
	gpu->name(VK_OBJECT_TYPE_PIPELINE, uint64_t(made), p.name_);
	p.error_.clear();
	++p.version_;
	if (sink_ && (p.version_ > 1 || was_broken)) {
		sink_(true, p.name_ + (was_broken ? " builds again" : " reloaded"));
	}
	return true;
}

bool ShaderLibrary::poll(bool immediately)
{
	const double now = now_seconds();
	if (!immediately && now - last_poll_ < 0.25) {
		return false;
	}
	last_poll_ = now;
	bool rebuilt = false;
	std::error_code ignored;
	for (auto &p : pipelines_) {
		bool changed = false;
		for (const auto &f : p->files_) {
			const auto time = fs::last_write_time(f, ignored);
			const auto it = std::find_if(times_.begin(), times_.end(), [&](const auto &t) { return t.first == f.string(); });
			if (!ignored && it != times_.end() && time != it->second) {
				changed = true;
			}
		}
		if (changed) {
			build(*p);
			rebuilt = true;
		}
	}
	return rebuilt;
}

void ShaderLibrary::reload_all()
{
	for (auto &p : pipelines_) {
		build(*p);
	}
}

} // namespace rimgui::rt::vk
