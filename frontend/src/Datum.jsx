import React from 'react'

const Datum = (props) => {
    return (
        <div className='flex-row input-container'>
            <span className='input-label ' style={{width: '80px'}} >{props.label}:</span>
            <div className='input-divider'/>
            <span className='input-label dark' style={{width: '80px'}} id={"data-box-" + props.label} >
                {Math.floor(props.value*100)/100}
            </span>
        </div>
    )
}

export default Datum
