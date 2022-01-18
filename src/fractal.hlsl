#include "fullscreenvert.hlsli"

[[vk::push_constant]]
ConstantBuffer<FullscreenPushConstants> consts : register(b0);

// Created by evilryu
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.

// whether turn on the animation
// #define phase_shift_on 

void ry(inout float3 p, float a){
    float3 q=p;
    float c = cos(a);
    float s = sin(a);
    p.x = c * q.x + s * q.z;
    p.z = -s * q.x + c * q.z;
}

/* 

z = r*(sin(theta)cos(phi) + i cos(theta) + j sin(theta)sin(phi)

zn+1 = zn^8 +c

z^8 = r^8 * (sin(8*theta)*cos(8*phi) + i cos(8*theta) + j sin(8*theta)*sin(8*theta)

zn+1' = 8 * zn^7 * zn' + 1

*/
float3 mb(float3 p) {
    float3 z = p;
    float3 dz= float3(0,0,0);
    float power = 8.0;
    float r, theta, phi;
    float dr = 1.0;
    float t0 = 1.0;

    for(int i = 0; i < 7; ++i) {
        r = length(z);
        if(r > 2.0) {
            continue;
        }
        theta = atan(z.y / z.x);
        #ifdef phase_shift_on
            phi = asin(z.z / r) + consts.time[0] * 0.1;
        #else
            phi = asin(z.z / r);
        #endif

        dr = pow(r, power - 1.0) * dr * power + 1.0;

        r = pow(r, power);
        theta = theta * power;
        phi = phi * power;

        z = r * float3(cos(theta)*cos(phi), sin(theta)*cos(phi), sin(phi)) + p;

        t0 = min(t0, r);
    }
    return float3(0.5 * log(r) * r / dr, t0, 0.0);
}

float3 f(float3 p) {
    ry(p, consts.time[0] * 0.2);
    return mb(p);
}

float softshadow(float3 ro, float3 rd, float k ){ 
    float akuma = 1.0;
    float h = 0.0;
    float t = 0.01;
    for(int i = 0; i < 50; ++i){ 
        h = f(ro + rd * t).x;
        if(h < 0.001) {
            return 0.02;
        }
        akuma = min(akuma, k * h / t);
        t += clamp(h, 0.01, 2.0);
    }
    return akuma;
}

float3 nor(in float3 pos){
    float3 eps = float3(0.001, 0.0, 0.0);
    return normalize( float3(
    f(pos+eps.xyy).x - f(pos-eps.xyy).x,
    f(pos+eps.yxy).x - f(pos-eps.yxy).x,
    f(pos+eps.yyx).x - f(pos-eps.yyx).x ) );
}

float3 intersect(float3 ro, float3 rd, float pixel_size)
{
    float t = 1.0;
    float res_t = 0.0;
    float res_d = 1000.0;
    float3 c, res_c;
    float max_error = 1000.0;
    float d = 1.0;
    float pd = 100.0;
    float os = 0.0;
    float step = 0.0;
    float error = 1000.0;

    for (int i=0; i < 48; i++)
    {
        if (error < pixel_size * 0.5 || t > 20.0){
        }
        else{  // avoid broken shader on windows
            c = f(ro + rd * t);
            d = c.x;

            if(d > os){
                os = 0.4 * d * d / pd;
                step = d + os;
                pd = d;
            }
            else{
                step =-os;
                os = 0.0;
                pd = 100.0; 
                d = 1.0;
            }

            error = d / t;

            if(error < max_error) {
                max_error = error;
                res_t = t;
                res_c = c;
            }
            t += step;
        }
    }

    if( t > 20.0 /* || max_error > pixel_size */ ) {
        res_t=-1.0;
    }

    return float3(res_t, res_c.y, res_c.z);
}

float4 frag(Interpolators i) : SV_TARGET
{ 
    const float seconds = consts.time[0];
    const float2 resolution = consts.resolution;

    float2 q = i.uv0.xy;
    float2 uv = -1.0 + 2.0 * q;
    uv.x *= resolution.x / resolution.y; 

    float pixel_size = 1.0 / (resolution.x * 3.0);
    float stime = 0.7 + 0.3 * sin(seconds * 0.4); 
    float ctime = 0.7 + 0.3 * cos(seconds * 0.4);

    float3 ta = float3(0.0,0.0,0.0); 
    float3 ro = float3(0.0, 3.*stime*ctime, 3.*(1.-stime*ctime));

    float3 cf = normalize(ta-ro); 
    float3 cs = normalize(cross(cf,float3(0.0,1.0,0.0))); 
    float3 cu = normalize(cross(cs,cf)); 
    float3 rd = normalize(uv.x * cs + uv.y * cu + 3.0 * cf);  // transform from view to world

    float3 sundir = normalize(float3(0.1, 0.8, 0.6)); 
    float3 sun = float3(1.64, 1.27, 0.99); 
    float3 skycolor = float3(0.6, 1.5, 1.0); 

    float3 bg = exp(uv.y-2.0)*float3(0.4, 1.6, 1.0);

    float halo = clamp(dot(normalize(float3(-ro.x, -ro.y, -ro.z)), rd), 0.0, 1.0); 
    float3 col = bg + float3(1.0,0.8,0.4) * pow(halo,17.0); 

    float t = 0.0;
    float3 p = ro;

    float3 res = intersect(ro, rd, pixel_size);
    if(res.x > 0.0){
        p = ro + res.x * rd;
        float3 n=nor(p); 
        float shadow = softshadow(p, sundir, 10.0 );

        float dif = max(0.0, dot(n, sundir)); 
        float sky = 0.6 + 0.4 * max(0.0, dot(n, float3(0.0, 1.0, 0.0))); 
        float bac = max(0.3 + 0.7 * dot(float3(-sundir.x, -1.0, -sundir.z), n), 0.0); 
        float spe = max(0.0, pow(clamp(dot(sundir, reflect(rd, n)), 0.0, 1.0), 10.0)); 

        float3 lin = 4.5 * sun * dif * shadow; 
        lin += 0.8 * bac * sun; 
        lin += 0.6 * sky * skycolor*shadow; 
        lin += 3.0 * spe * shadow; 

        res.y = pow(clamp(res.y, 0.0, 1.0), 0.55);
        float3 tc0 = 0.5 + 0.5 * sin(3.0 + 4.2 * res.y + float3(0.0, 0.5, 1.0));
        col = lin *float3(0.9, 0.8, 0.6) *  0.2 * tc0;
        col=lerp(col,bg, 1.0-exp(-0.001*res.x*res.x)); 
    }

    // post
    col=pow(clamp(col,0.0,1.0),float3(0.45, 0.45, 0.45)); 
    col=col*0.6+0.4*col*col*(3.0-2.0*col);  // contrast
    float intermediate = dot(col, float3(0.33, 0.33, 0.33));
    col=lerp(col, float3(intermediate, intermediate, intermediate), -0.5);  // satuation
    col*=0.5+0.5*pow(16.0*q.x*q.y*(1.0-q.x)*(1.0-q.y),0.7);  // vigneting
    return float4(col.xyz, smoothstep(0.55, .76, 1.-res.x/5.)); 
}