# Compatibility Issue Flags



You may notice that there is usually a straight line displayed on MetalHUD, and sometimes some letters also appear on it. They are `Compatibility Issue Flags` that indicates some operations are not properly handled by DXMT (because they are not supported **yet**). 

Possible flags:
- `---G-----------------------`: certain Geometry Shader is not supported
- `-----TO--------------------`: certain Tessellator output primitive is not supported (point/isoline) 
- `--------IT-----------------`: Indirect Tessellation Draw not supported
- `-----------GT--------------`: Geometry-Tessellation pipeline not supported
- `--------------A------------`: `DrawAuto()` not supported
- `----------------P----------`: Predicated command not supported
- `------------------SA-------`: Stream Output Appending not supported
- `---------------------MS----`: Multiple SO Stream not supported

It can be a combination of them as well.

When only a straight line appears, it probably means everything is fine. Otherwise the rendering outcome might be incorrect (although it's not always noticeable).