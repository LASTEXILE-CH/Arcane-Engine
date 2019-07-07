#include "pch.h"
#include "DeferredLightingPass.h"

#include <graphics/renderer/renderpass/deferred/DeferredGeometryPass.h>
#include <utils/loaders/ShaderLoader.h>

namespace arcane {

	DeferredLightingPass::DeferredLightingPass(Scene3D *scene) : RenderPass(scene), m_AllocatedFramebuffer(true)
	{
		m_LightingShader = ShaderLoader::loadShader("src/shaders/deferred/pbr_lighting_pass.vert", "src/shaders/deferred/pbr_lighting_pass.frag");

		m_Framebuffer = new Framebuffer(Window::getWidth(), Window::getHeight(), false);
		m_Framebuffer->addColorTexture(FloatingPoint16).addDepthStencilTexture(NormalizedDepthStencil).createFramebuffer();
	}

	DeferredLightingPass::DeferredLightingPass(Scene3D *scene, Framebuffer *customFramebuffer) : RenderPass(scene), m_AllocatedFramebuffer(false), m_Framebuffer(customFramebuffer)
	{
		m_LightingShader = ShaderLoader::loadShader("src/shaders/deferred/pbr_lighting_pass.vert", "src/shaders/deferred/pbr_lighting_pass.frag");
	}

	DeferredLightingPass::~DeferredLightingPass() {
		if (m_AllocatedFramebuffer) {
			delete m_Framebuffer;
		}
	}

	LightingPassOutput DeferredLightingPass::executeLightingPass(ShadowmapPassOutput &shadowmapData, GeometryPassOutput &geometryData, ICamera *camera, bool useIBL) {
		// Framebuffer setup
		glViewport(0, 0, m_Framebuffer->getWidth(), m_Framebuffer->getHeight());
		m_Framebuffer->bind();
		m_Framebuffer->clear();
		m_GLCache->setDepthTest(false);
		m_GLCache->setMultisample(false);

		// Move the depth + stencil of the GBuffer to the our framebuffer
		// NOTE: Framebuffers have to have identical depth + stencil formats for this to work
		glBindFramebuffer(GL_READ_FRAMEBUFFER, geometryData.outputGBuffer->getFramebuffer());
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_Framebuffer->getFramebuffer());
		glBlitFramebuffer(0, 0, geometryData.outputGBuffer->getWidth(), geometryData.outputGBuffer->getHeight(), 0, 0, m_Framebuffer->getWidth(), m_Framebuffer->getHeight(), GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);

		// Setup initial stencil state
		m_GLCache->setStencilTest(true);
		m_GLCache->setStencilWriteMask(0x00); // Do not update stencil values

		DynamicLightManager *lightManager = m_ActiveScene->getDynamicLightManager();
		ProbeManager *probeManager = m_ActiveScene->getProbeManager();

		m_GLCache->switchShader(m_LightingShader);
		lightManager->bindLightingUniforms(m_LightingShader);
		m_LightingShader->setUniform3f("viewPos", camera->getPosition());
		m_LightingShader->setUniformMat4("viewInverse", glm::inverse(camera->getViewMatrix()));
		m_LightingShader->setUniformMat4("projectionInverse", glm::inverse(camera->getProjectionMatrix()));

		// Bind GBuffer data
		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D, geometryData.outputGBuffer->getAlbedo());
		m_LightingShader->setUniform1i("albedoTexture", 4);

		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, geometryData.outputGBuffer->getNormal());
		m_LightingShader->setUniform1i("normalTexture", 5);

		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_2D, geometryData.outputGBuffer->getMaterialInfo());
		m_LightingShader->setUniform1i("materialInfoTexture", 6);

		glActiveTexture(GL_TEXTURE7);
		glBindTexture(GL_TEXTURE_2D, geometryData.outputGBuffer->getDepthStencilTexture());
		m_LightingShader->setUniform1i("depthTexture", 7);

		m_LightingShader->setUniform1f("nearPlane", NEAR_PLANE);
		m_LightingShader->setUniform1f("farPlane", FAR_PLANE);

		// Shadowmap code
		bindShadowmap(m_LightingShader, shadowmapData);

		// Finally perform the lighting using the GBuffer
		ModelRenderer *modelRenderer = m_ActiveScene->getModelRenderer();

		// IBL Binding
		probeManager->bindProbes(glm::vec3(0.0f, 0.0f, 0.0f), m_LightingShader);

		// Perform lighting on the terrain (turn IBL off)
		m_LightingShader->setUniform1i("computeIBL", 0);
		glStencilFunc(GL_EQUAL, DeferredStencilValue::TerrainStencilValue, 0xFF);
		modelRenderer->NDC_Plane.Draw();

		// Perform lighting on everything else (turn IBL on)
		if (useIBL) {
			m_LightingShader->setUniform1i("computeIBL", 1);
		}
		glStencilFunc(GL_NOTEQUAL, DeferredStencilValue::TerrainStencilValue, 0xFF);
		modelRenderer->NDC_Plane.Draw();


		// Reset state
		m_GLCache->setDepthTest(true);
		m_GLCache->setStencilTest(false);

		// Render pass output
		LightingPassOutput passOutput;
		passOutput.outputFramebuffer = m_Framebuffer;
		return passOutput;
	}

	void DeferredLightingPass::bindShadowmap(Shader *shader, ShadowmapPassOutput &shadowmapData) {
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, shadowmapData.shadowmapFramebuffer->getDepthStencilTexture());
		shader->setUniform1i("shadowmap", 0);
		shader->setUniformMat4("lightSpaceViewProjectionMatrix", shadowmapData.directionalLightViewProjMatrix);
	}

}