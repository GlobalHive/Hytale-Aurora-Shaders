#include "../../hook/aud_framegen.cpp"
#include <assert.h>

static sl::float4 transform(const sl::float4 &point, const sl::float4x4 &matrix)
{
    const float *components = &point.x;
    sl::float4 result(0, 0, 0, 0);
    float *output = &result.x;
    for (int column = 0; column < 4; column++)
        for (int row = 0; row < 4; row++) output[column] += components[row] * (&matrix[row].x)[column];
    return result;
}

int main()
{
    float scene[152] = {}, previous[152] = {};
    identity(scene + 32); identity(scene + 80);
    scene[48] = 1; scene[53] = 1.7f;
    scene[58] = -(1000.0f + 0.1f) / (1000.0f - 0.1f);
    scene[59] = -1; scene[62] = -2.0f * 1000.0f * 0.1f / (1000.0f - 0.1f);
    scene[144] = 0.1f; scene[145] = 1000.0f;
    memcpy(previous, scene, sizeof(scene));
    renderWidth = 1280; renderHeight = 720;
    float jitter[2] = {0.5f / renderWidth, 0.5f / renderHeight};
    scene[56] = 0.001f; scene[57] = -0.001f;
    previous[56] = -0.002f; previous[57] = 0.002f;
    sl::Constants values;
    assert(constants(values, scene, previous, jitter, 1));
    assert(values.reset == sl::eTrue && values.cameraMotionIncluded == sl::eTrue);
    assert(fabsf(values.jitterOffset.x - 0.25f) < 1e-6f && fabsf(values.jitterOffset.y + 0.25f) < 1e-6f);
    sl::float4 nearPoint = transform({0, 0, -0.1f, 1}, values.cameraViewToClip);
    sl::float4 farPoint = transform({0, 0, -1000.0f, 1}, values.cameraViewToClip);
    assert(fabsf(nearPoint.z / nearPoint.w) < 1e-6f);
    assert(fabsf(farPoint.z / farPoint.w - 1) < 1e-6f);
    for (int row = 0; row < 4; row++)
        for (int column = 0; column < 4; column++) assert(fabsf((&values.clipToPrevClip[row].x)[column] - (row == column ? 1.0f : 0.0f)) < 1e-5f);
    sl::float4 viewPoint(0.25f, 0.5f, -10, 1);
    sl::float4 clip = transform(viewPoint, values.cameraViewToClip);
    sl::float4 reconstructed = transform(clip, values.clipToCameraView);
    for (int index = 0; index < 4; index++) assert(fabsf((&reconstructed.x)[index] - (&viewPoint.x)[index]) < 0.0001f);
    scene[132] = 0.5f;
    assert(constants(values, scene, previous, jitter, 0));
    sl::float4 projectedPrevious = transform(clip, values.clipToPrevClip);
    sl::float4 expectedPrevious = transform({0.75f, 0.5f, -10, 1}, values.cameraViewToClip);
    for (int index = 0; index < 4; index++) assert(fabsf((&projectedPrevious.x)[index] - (&expectedPrevious.x)[index]) < 0.0001f);
    puts("PASS: row-vector matrices, GL-to-Vulkan depth/orientation, jitter removal, inverse projection, camera-relative reprojection");
    return 0;
}
