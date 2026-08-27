Texture2D<float4> input_texture : register(t0);
SamplerState input_sampler : register(s0);

cbuffer ConverterConfig : register(b0) {
    uint hdr_output;
    uint uv_plane;
    float2 padding;
};

struct PS_INPUT {
    float4 position : SV_POSITION;
    float2 tex_coord : TEXCOORD0;
};

float3 ApplySRGBCurve(float3 x) {
    x = saturate(x);
    return x < 0.0031308 ? 12.92 * x
                         : 1.13005 * sqrt(x - 0.00228) - 0.13448 * x + 0.005719;
}

float3 NitsToPQ(float3 value) {
    static const float m1 = 2610.0 / 4096.0 / 4.0;
    static const float m2 = 2523.0 / 4096.0 * 128.0;
    static const float c1 = 3424.0 / 4096.0;
    static const float c2 = 2413.0 / 4096.0 * 32.0;
    static const float c3 = 2392.0 / 4096.0 * 32.0;
    float3 powered = pow(saturate(value / 10000.0), m1);
    return pow((c1 + c2 * powered) / (1.0 + c3 * powered), m2);
}

float3 ScRgbToPq(float3 rgb) {
    static const float3x3 rec709_to_rec2020 = {
        0.627402, 0.329292, 0.043306,
        0.069095, 0.919544, 0.011360,
        0.016394, 0.088028, 0.895578
    };
    return NitsToPQ(mul(rec709_to_rec2020, rgb) * 80.0);
}

float4 PS(PS_INPUT input) : SV_TARGET {
    float3 rgb = input_texture.Sample(input_sampler, input.tex_coord).rgb;
    rgb = hdr_output != 0 ? ScRgbToPq(rgb) : ApplySRGBCurve(rgb);

    float kr = hdr_output != 0 ? 0.2627 : 0.2126;
    float kb = hdr_output != 0 ? 0.0593 : 0.0722;
    float kg = 1.0 - kr - kb;
    float maximum = hdr_output != 0 ? 1023.0 : 255.0;
    float y_scale = (hdr_output != 0 ? 876.0 : 219.0) / maximum;
    float y_offset = (hdr_output != 0 ? 64.0 : 16.0) / maximum;
    float uv_scale = (hdr_output != 0 ? 896.0 : 224.0) / maximum;
    float uv_offset = (hdr_output != 0 ? 512.0 : 128.0) / maximum;

    if (uv_plane != 0) {
        float u = dot(float3(-0.5 * kr / (1.0 - kb),
                             -0.5 * kg / (1.0 - kb), 0.5), rgb);
        float v = dot(float3(0.5, -0.5 * kg / (1.0 - kr),
                             -0.5 * kb / (1.0 - kr)), rgb);
        return float4(saturate(u * uv_scale + uv_offset),
                      saturate(v * uv_scale + uv_offset), 0.0, 1.0);
    }

    float y = dot(float3(kr, kg, kb), rgb);
    return float4(saturate(y * y_scale + y_offset), 0.0, 0.0, 1.0);
}
